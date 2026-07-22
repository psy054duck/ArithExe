#include "VerificationSession.h"

#include <stdexcept>

using namespace ari_exe;

thread_local VerificationSession* VerificationSession::active_session =
    nullptr;

VerificationSession::Activation::Activation(VerificationSession& session)
    : previous_session(active_session),
      previous_manager(AnalysisManager::set_active(&session.analysis_manager)) {
    active_session = &session;
}

VerificationSession::Activation::~Activation() {
    active_session = previous_session;
    AnalysisManager::set_active(previous_manager);
}

VerificationSession::~VerificationSession() {
    clear_module_state();
}

VerificationSession& VerificationSession::current() {
    if (!active_session) {
        throw std::logic_error("no active verification session");
    }
    return *active_session;
}

void VerificationSession::clear_module_state() {
    instruction_cache.clear();
    function_summary_cache = SymbolTable<FunctionSummary>();
    loop_summary_cache = SymbolTable<LoopSummary>();
    cached_function_values.clear();
    solver_worker.reset();
    call_value_counters.clear();
    failed_loops.clear();
    memory_object_name_counters.clear();
    analysis_manager.clear_module_state();
}
