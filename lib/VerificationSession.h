#ifndef VERIFICATION_SESSION_H
#define VERIFICATION_SESSION_H

#include <map>
#include <memory>
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

    // Experimental IR-level relaxation, not a proof of fixed-width C semantics.
    // Configure before starting execution; do not mix cached semantic profiles.
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

    SymbolTable<LoopSummary>& loop_summaries() {
        return loop_summary_cache;
    }

    Cache& function_cache() { return cached_function_values; }

    RecurrenceSolverWorker& recurrence_solver_worker();

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
    SymbolTable<LoopSummary> loop_summary_cache;
    Cache cached_function_values;
    std::shared_ptr<RecurrenceSolverWorker> solver_worker;
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
    std::map<llvm::Loop*, std::shared_ptr<NestedPathSummary>>
        nested_path_summary_cache;
    std::uint64_t nested_path_summaries_built = 0;
};

} // namespace ari_exe

#endif
