#ifndef STATE_H
#define STATE_H

#include <map>
#include <optional>
#include <vector>
#include <set>

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Constants.h"

#include "z3++.h"

#include "Memory.h"
#include "Expr.h"
#include "PathExpression.h"

namespace llvm {
    class BasicBlock;
    class Loop;
}

namespace ari_exe {
    class AInstruction;
    class AStack;
    class State;
    class LoopState;
    class RecState;
    class VerificationSession;

    using trace_ty = std::vector<llvm::BasicBlock*>;

    template<typename state_ty>
    using state_ptr_base = std::shared_ptr<state_ty>;

    using state_ptr = state_ptr_base<State>;
    using loop_state_ptr = state_ptr_base<LoopState>;
    using rec_state_ptr = state_ptr_base<RecState>;

    template<typename state_ty> 
    using state_list_base = std::vector<state_ptr_base<state_ty>>;

    using state_list = state_list_base<State>;
    using loop_state_list = state_list_base<LoopState>;
    using rec_state_list = state_list_base<RecState>;
}

#include "AInstruction.h"
#include "SymbolTable.h"
#include "MStack.h"
#include "FunctionSummary.h"
#include "LoopSummary.h"

namespace ari_exe {
    struct SymbolicPathPower {
        PathWord root;
        z3::expr exponent;
    };

    struct ExactPathPrefixSegment {
        PathWord explicit_word;
        std::optional<SymbolicPathPower> symbolic_power;

        static ExactPathPrefixSegment explicit_segment(PathWord word) {
            return {std::move(word), std::nullopt};
        }

        static ExactPathPrefixSegment symbolic_segment(PathWord root,
                                                       const z3::expr& exponent) {
            return {{}, SymbolicPathPower{std::move(root), exponent}};
        }
    };

    struct LoopPathExpressionState {
        std::vector<ExactPathPrefixSegment> prefix;
        std::vector<PathSchemaCandidate> candidates;
        std::size_t decision_cursor = 0;
        bool decision_cursor_initialized = false;
        // Saved before PHIs/observations for this invocation. Coarse fallback
        // is independent of the precise prefix and its coverage complement.
        state_ptr coarse_entry;
        std::size_t observed_iterations = 0;
        bool coarse_attempted = false;
        bool safety_attempted = false;

        void append(PathSymbol symbol) {
            if (prefix.empty() || prefix.back().symbolic_power.has_value()) {
                prefix.push_back(
                    ExactPathPrefixSegment::explicit_segment({symbol}));
            } else {
                prefix.back().explicit_word.push_back(symbol);
            }
        }

        void append_symbolic_power(const PathWord& root,
                                   const z3::expr& exponent) {
            prefix.push_back(
                ExactPathPrefixSegment::symbolic_segment(root, exponent));
        }

        const PathWord& concrete_frontier() const {
            static const PathWord empty;
            if (prefix.empty() || prefix.back().symbolic_power.has_value()) {
                return empty;
            }
            return prefix.back().explicit_word;
        }

        std::vector<PathPrefixBlock> compression_prefix() const {
            std::vector<PathPrefixBlock> result;
            for (const auto& segment : prefix) {
                if (!segment.symbolic_power) {
                    result.push_back({segment.explicit_word, 1, {}});
                    continue;
                }
                const auto& power = *segment.symbolic_power;
                std::uint64_t count = 0;
                auto exponent = power.exponent.simplify();
                if (exponent.is_numeral_u64(count))
                    result.push_back({power.root, count, {}});
                else
                    result.push_back({power.root, 0, exponent.to_string()});
            }
            return result;
        }
    };

    struct NondetCall {
        llvm::CallInst* instruction;
        std::optional<z3::expr> value;
        std::optional<z3::func_decl> values;
        std::optional<z3::expr> count;
        // A constructed sequence after existentially projecting pure control
        // choices out of the path condition. This retains a concrete witness.
        std::optional<z3::expr> sequence;

        NondetCall(llvm::CallInst* instruction, const z3::expr& value)
            : instruction(instruction), value(value) {}

        NondetCall(llvm::CallInst* instruction, const z3::func_decl& values,
                   const z3::expr& count)
            : instruction(instruction), values(values), count(count) {}

        NondetCall(llvm::CallInst* instruction, const z3::expr& sequence,
                   const z3::expr& count)
            : instruction(instruction), count(count), sequence(sequence) {}
    };

    class State {
        private:
            // path condition collected so far
            Expression path_condition;

        public:
            enum Status {
                RUNNING,        // normal status
                TERMINATED,     // reach the end of the program
                VERIFYING,      // a verification condition is being generated
                TESTING,        // a new branch, should check the feasibility
                REACH_ERROR,    // reach an error state (call reach_error())
                UNKNOWN,
                FAIL,
            };

        public:
            State(VerificationSession& session, z3::context& z3ctx,
                  AInstruction* pc, AInstruction* prev_pc,
                  const Memory& memory, const Expression& path_condition,
                  const trace_ty& trace, Status status = RUNNING)
                : session(session), z3ctx(z3ctx), pc(pc), prev_pc(prev_pc),
                  memory(memory), path_condition(path_condition), trace(trace),
                  status(status) {};
            State(const State& state): session(state.session), z3ctx(state.z3ctx), pc(state.pc), prev_pc(state.prev_pc), memory(state.memory), path_condition(state.path_condition), trace(state.trace), status(state.status), verification_condition(state.verification_condition), summary_invariants(state.summary_invariants), is_over_approx(state.is_over_approx), feasibility_uncertain(state.feasibility_uncertain), summary_arithmetic(state.summary_arithmetic), coarse_fallback_state(state.coarse_fallback_state), safety_resume_state(state.safety_resume_state), preserve_loop_entry_history(state.preserve_loop_entry_history), coarse_loop(state.coarse_loop), coarse_degree(state.coarse_degree), coarse_refining(state.coarse_refining), coarse_replay_loop(state.coarse_replay_loop), coarse_replay_one_block(state.coarse_replay_one_block), coarse_boundary_states(state.coarse_boundary_states), nondet_calls(state.nondet_calls), counterexample_complete(state.counterexample_complete), loop_certificates(state.loop_certificates), function_certificates(state.function_certificates), path_decisions(state.path_decisions), loop_path_expressions(state.loop_path_expressions) {};

            // if the state is in the process of summarizing a loop
            virtual bool is_summarizing() const { return false; }

            // The optional arithmetic bypass belongs only to internal summary
            // probes, never to ordinary execution or exact fallback/replay.
            // Explicitly copied across successors that erase the LoopState type.
            bool uses_relaxed_arithmetic() const;
            bool ignores_integer_width(unsigned width) const;

            virtual void append_path_condition(const Expression& _path_condition);

            /**
             * @brief check if the given e is concrete and equal to concrete_value in the current state
             */
            bool is_concrete(const Expression& e, const Expression& concrete_value);

            // step the pc
            void step_pc(AInstruction* next_pc = nullptr);

            // PHIs are parallel assignments on the incoming predecessor edge.
            void execute_phi_bundle();

            // Source-type domains of recognized nondeterministic APIs. Returns
            // true when a domain was applied; signedness comes from the API,
            // not the signless LLVM return type.
            bool constrain_nondet_input(llvm::CallInst* call,
                                       const Expression& value);

            VerificationSession& session;

            // The context for Z3
            z3::context& z3ctx;

            // The next instruction to be executed
            AInstruction* pc;

            AInstruction* prev_pc;

            bool is_over_approx = false;
            // Informational only. Full assertion/error queries, not an earlier
            // inconclusive branch query, decide safety and counterexamples.
            bool feasibility_uncertain = false;
            bool summary_arithmetic = false;
            // Immutable pre-PHI entry for independent coarse degree retries.
            // The engine isolates its worklist/results and rolls back the
            // whole proof attempt, not individual approximate successors.
            state_ptr coarse_fallback_state;
            // Resume the unchanged exact pre-PHI trial if a safety-first
            // transaction fails; no coverage complement has been added yet.
            state_ptr safety_resume_state;
            // One-shot marker for resuming the identical pre-PHI entry.
            bool preserve_loop_entry_history = false;
            llvm::Loop* coarse_loop = nullptr;
            unsigned coarse_degree = 0;
            bool coarse_refining = false;

            // Independent proof boundary: replay one complete iteration, or
            // one CFG block for a flattened nest, from an arbitrary certified
            // prefix. Internal assertions and every outgoing edge are checked.
            // Returning edges are already covered by the certified union-star.
            llvm::Loop* coarse_replay_loop = nullptr;
            bool coarse_replay_one_block = false;
            std::vector<state_ptr> coarse_boundary_states;

            // memory model for symbolic execution
            Memory memory;

            Expression evaluate(llvm::Value* v, bool is_signed = true);

            trace_ty trace;

            // The status of the state
            Status status = RUNNING;

            // verification condition
            Expression verification_condition = z3ctx.bool_val(true);

            // Assertions encountered while collecting a recursive summary.
            // They are used only as candidates and must pass an inductiveness
            // check before entering a summary.
            std::vector<Expression> summary_invariants;

            Expression get_path_condition() const { return path_condition; }

            /**
             * @brief get a model for current path condition
             */
            z3::model get_model();

            std::optional<z3::model> model;

            // Nondeterministic function calls made on this execution path, in
            // dynamic execution order. Their symbolic return values are
            // evaluated when a feasible violation is found.
            std::vector<NondetCall> nondet_calls;

            // False if a summary hides a nondeterministic call whose dynamic
            // return sequence cannot be reconstructed soundly.
            bool counterexample_complete = true;

            // Exact scalar loop summaries encountered on this path. These are
            // converted to source-level invariants for correctness witnesses.
            std::vector<LoopCertificate> loop_certificates;

            // Exact recursive summaries encountered on this path. These are
            // converted to source-level function contracts.
            std::vector<FunctionCertificate> function_certificates;

            // Conditional branch/select choices, independent of the legacy
            // basic-block trace used by PHI execution.
            std::vector<PathDecisionEvent> path_decisions;

            // Exact per-loop path prefixes. Compression candidates are evidence
            // only; only certified accelerations may append symbolic powers.
            std::map<llvm::Loop*, LoopPathExpressionState>
                loop_path_expressions;
    };

    class LoopState: public State {
        public:
            LoopState(VerificationSession& session, z3::context& z3ctx,
                      AInstruction* pc, AInstruction* prev_pc,
                      const Memory& memory, const Expression& path_condition,
                      const Expression& path_condition_in_loop,
                      const trace_ty& trace, Status status = RUNNING);
            LoopState(const State& state): State(state), path_condition_in_loop(state.z3ctx.bool_val(true)), summarizing_loop(nullptr) {};
            LoopState(const LoopState& state): State(state), path_condition_in_loop(state.path_condition_in_loop), summarizing_loop(state.summarizing_loop) {};

            // if the state is in the process of summarizing a loop
            bool is_summarizing() const override { return true; }

            // append a new path condition to the current state
            // besides, it also appends the path condition in loop body
            // if this state is not exiting the loop
            void append_path_condition(const Expression& _path_condition) override;

            // path condition in loop body, loop guard is discarded
            // should only be used when summarize a loop
            Expression path_condition_in_loop;

            llvm::Loop* summarizing_loop = nullptr;

            std::map<llvm::Instruction*, z3::expr> unknown_call_counters;

            /**
             * @brief store modified values by the loop
             */
            // std::set<llvm::Value*> modified_values;
    };

    class RecState: public State {
        public:
            RecState(VerificationSession& session, z3::context& z3ctx,
                     AInstruction* pc, AInstruction* prev_pc,
                     const Memory& memory, const Expression& path_condition,
                     const Expression& path_condition_in_loop,
                     const trace_ty& trace, Status status = RUNNING);
            RecState(const State& state): State(state) {};
            RecState(const RecState& state): State(state) {};

            // if the state is in the process of summarizing a loop
            bool is_summarizing() const override { return true; }
    };
}

#endif
