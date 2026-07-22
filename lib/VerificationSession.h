#ifndef VERIFICATION_SESSION_H
#define VERIFICATION_SESSION_H

#include <map>
#include <memory>
#include <set>
#include <string>

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "AnalysisManager.h"
#include "FunctionSummary.h"
#include "LoopSummary.h"
#include "SymbolTable.h"
#include "cache.h"

namespace ari_exe {

class AInstruction;
class RecurrenceSolverWorker;

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

    void clear_module_state();

  private:
    static thread_local VerificationSession* active_session;

    AnalysisManager analysis_manager;
    std::map<llvm::Instruction*, std::shared_ptr<AInstruction>>
        instruction_cache;
    SymbolTable<FunctionSummary> function_summary_cache;
    SymbolTable<LoopSummary> loop_summary_cache;
    Cache cached_function_values;
    std::shared_ptr<RecurrenceSolverWorker> solver_worker;
    std::map<llvm::Value*, int> call_value_counters;
    std::set<llvm::Loop*> failed_loops;
    std::map<std::string, unsigned> memory_object_name_counters;
};

} // namespace ari_exe

#endif
