#ifndef VERIFICATION_SESSION_H
#define VERIFICATION_SESSION_H

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "AnalysisManager.h"
#include "FunctionSummary.h"
#include "LoopSummary.h"
#include "SymbolTable.h"
#include "cache.h"
#include "PathExpression.h"

namespace ari_exe {

class AInstruction;
class RecurrenceSolverWorker;
struct NestedPathSummary;
struct ScalarStateTransformer;

class VerificationSession {
  public:
    class Activation {
      public:
        explicit Activation(VerificationSession& session);
        ~Activation();

        Activation(const Activation&) = delete;
        Activation& operator=(const Activation&) = delete;

      private:
        VerificationSession* previous_session;
        AnalysisManager* previous_manager;
    };

    VerificationSession() = default;
    ~VerificationSession();

    VerificationSession(const VerificationSession&) = delete;
    VerificationSession& operator=(const VerificationSession&) = delete;

    Activation activate() { return Activation(*this); }

    static VerificationSession& current();

    // Experimental summary-only IR relaxation, not a fixed-width C proof.
    // Ordinary execution always retains machine-width semantics. Configure
    // before starting execution; do not mix cached summary profiles.
    void set_ignore_bitwidth_constraints(bool enabled) {
        ignore_bitwidth_constraints_enabled = enabled;
    }
    // Compatibility alias; now relaxes all integer widths, not only i32.
    void set_ignore_32bit_constraints(bool enabled) {
        set_ignore_bitwidth_constraints(enabled);
    }
    bool uses_integer_relaxation() const {
        return ignore_bitwidth_constraints_enabled;
    }
    bool ignores_integer_width(unsigned width) const {
        return ignore_bitwidth_constraints_enabled && width > 1;
    }

    AnalysisManager& analyses() { return analysis_manager; }

    std::map<llvm::Instruction*, std::shared_ptr<AInstruction>>&
    instructions() {
        return instruction_cache;
    }

    SymbolTable<FunctionSummary>& function_summaries() {
        return function_summary_cache;
    }
    SymbolTable<FunctionSummary>& mathematical_helper_summaries() { return mathematical_helper_cache; }
    std::map<llvm::Function*, std::shared_ptr<ScalarStateTransformer>>& state_transformer_summaries() { return state_transformer_cache; }

    SymbolTable<LoopSummary>& loop_summaries() {
        return loop_summary_cache;
    }

    Cache& function_cache() { return cached_function_values; }

    RecurrenceSolverWorker& recurrence_solver_worker();

    // Trusted arithmetic lemmas are scoped to these queries, never precise
    // execution. Retry without quantifiers on UNKNOWN within the same budget.
    z3::check_result check_coarse_query(z3::solver& solver, unsigned timeout_ms);
    std::uint64_t coarse_lemma_query_count() const { return coarse_lemma_queries; }
    std::uint64_t coarse_lemma_retry_count() const { return coarse_lemma_retries; }
    unsigned coarse_lemma_count() const {
        return coarse_lemmas ? coarse_lemmas->size() : 0;
    }

    int next_call_value_id(llvm::Value* value) {
        return call_value_counters[value]++;
    }

    bool loop_summary_failed(llvm::Loop* loop) const {
        return failed_loops.contains(loop);
    }

    void mark_loop_summary_failed(llvm::Loop* loop) {
        failed_loops.insert(loop);
    }

    unsigned memory_object_name_id(const std::string& name) {
        return memory_object_name_counters[name];
    }

    PathSymbol intern_loop_path(
        llvm::Loop* loop,
        const std::vector<PathDecisionEvent>& decisions);

    std::uint64_t next_path_expression_id() {
        return path_expression_id++;
    }

    void note_path_compression() { ++path_compressions; }
    void note_path_acceleration() { ++path_accelerations; }
    void note_path_affine_template() { ++path_affine_templates; }
    std::uint64_t path_compression_count() const {
        return path_compressions;
    }
    std::uint64_t path_acceleration_count() const {
        return path_accelerations;
    }
    std::uint64_t path_affine_template_count() const {
        return path_affine_templates;
    }

    void note_coarse_summary_attempt(unsigned degree = 0) {
        ++coarse_summary_attempts;
        if (degree) ++coarse_degree_attempts[degree];
    }
    std::uint64_t coarse_degree_attempt_count(unsigned degree) const {
        const auto found = coarse_degree_attempts.find(degree);
        return found == coarse_degree_attempts.end() ? 0 : found->second;
    }
    void note_coarse_summary() { ++coarse_summaries; }
    std::uint64_t coarse_summary_attempt_count() const {
        return coarse_summary_attempts;
    }
    std::uint64_t coarse_summary_count() const { return coarse_summaries; }
    void note_coarse_refinement() { ++coarse_refinements; }
    std::uint64_t coarse_refinement_count() const { return coarse_refinements; }

    void note_residue_attempt() { ++residue_attempts; }
    void note_residue_query() { ++residue_queries; }
    void note_residue_proof() { ++residue_proofs; }
    std::uint64_t residue_attempt_count() const { return residue_attempts; }
    std::uint64_t residue_query_count() const { return residue_queries; }
    std::uint64_t residue_proof_count() const { return residue_proofs; }

    std::map<llvm::Loop*, std::shared_ptr<NestedPathSummary>>&
    nested_path_summaries() { return nested_path_summary_cache; }
    void note_nested_path_summary() { ++nested_path_summaries_built; }
    std::uint64_t nested_path_summary_count() const {
        return nested_path_summaries_built;
    }

    void clear_module_state();

  private:
    static thread_local VerificationSession* active_session;

    AnalysisManager analysis_manager;
    bool ignore_bitwidth_constraints_enabled = false;
    std::map<llvm::Instruction*, std::shared_ptr<AInstruction>>
        instruction_cache;
    SymbolTable<FunctionSummary> function_summary_cache;
    SymbolTable<FunctionSummary> mathematical_helper_cache;
    std::map<llvm::Function*, std::shared_ptr<ScalarStateTransformer>> state_transformer_cache;
    SymbolTable<LoopSummary> loop_summary_cache;
    Cache cached_function_values;
    std::shared_ptr<RecurrenceSolverWorker> solver_worker;
    std::optional<z3::expr_vector> coarse_lemmas;
    std::uint64_t coarse_lemma_queries = 0;
    std::uint64_t coarse_lemma_retries = 0;
    std::map<llvm::Value*, int> call_value_counters;
    std::set<llvm::Loop*> failed_loops;
    std::map<std::string, unsigned> memory_object_name_counters;
    std::map<llvm::Loop*,
             std::map<std::vector<PathDecisionEvent>, PathSymbol>>
        loop_path_alphabets;
    PathSymbol next_path_symbol = 1;
    std::uint64_t path_expression_id = 1;
    std::uint64_t path_compressions = 0;
    std::uint64_t path_accelerations = 0;
    std::uint64_t path_affine_templates = 0;
    std::uint64_t coarse_summary_attempts = 0;
    std::uint64_t coarse_summaries = 0;
    std::uint64_t coarse_refinements = 0;
    std::uint64_t residue_attempts = 0, residue_queries = 0, residue_proofs = 0;
    std::map<unsigned, std::uint64_t> coarse_degree_attempts;
    std::map<llvm::Loop*, std::shared_ptr<NestedPathSummary>>
        nested_path_summary_cache;
    std::uint64_t nested_path_summaries_built = 0;
};

} // namespace ari_exe

#endif
