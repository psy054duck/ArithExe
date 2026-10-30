#include "VerificationSession.h"

#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>

#ifndef ARITHEXE_DEFAULT_COARSE_LEMMA_FILE
#define ARITHEXE_DEFAULT_COARSE_LEMMA_FILE "lemma.smt2"
#endif

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

PathSymbol VerificationSession::intern_loop_path(
    llvm::Loop* loop, const std::vector<PathDecisionEvent>& decisions) {
    auto& alphabet = loop_path_alphabets[loop];
    auto found = alphabet.find(decisions);
    if (found != alphabet.end()) return found->second;
    const PathSymbol symbol = next_path_symbol++;
    alphabet.emplace(decisions, symbol);
    return symbol;
}

z3::check_result VerificationSession::check_coarse_query(
    z3::solver& solver, unsigned timeout_ms) {
    auto& context = analysis_manager.get_z3ctx();
    if (&solver.ctx() != &context || !timeout_ms) {
        throw std::invalid_argument("coarse queries require the session context and a positive timeout");
    }
    if (!coarse_lemmas) {
        std::filesystem::path path = ARITHEXE_DEFAULT_COARSE_LEMMA_FILE;
        if (const char* requested = std::getenv("ARITHEXE_COARSE_LEMMA_FILE");
            requested && *requested) {
            path = requested;
        } else if (const char* worker = std::getenv("ARITHEXE_SOLVER_WORKER");
                   worker && *worker) {
            // Packaged/frozen workers use their own copy, not the live repo.
            auto candidate = std::filesystem::path(worker).parent_path() / "lemma.smt2";
            if (std::filesystem::is_regular_file(candidate)) path = candidate;
        }
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("missing coarse lemma file: " + path.string());
        }
        try {
            coarse_lemmas.emplace(context.parse_file(path.string().c_str()));
        } catch (const z3::exception& error) {
            throw std::runtime_error("cannot parse coarse lemma file " + path.string() + ": " + error.msg());
        }
        spdlog::debug("[coarse-path] loaded {} trusted arithmetic lemmas from {}",
                      coarse_lemmas->size(), path.string());
    }

    // Keep the original assertions for a clean retry/model. Quantified
    // division identities can obstruct SAT/MBQI even for simple guards.
    const auto original = solver.assertions();
    solver.add(*coarse_lemmas);
    ++coarse_lemma_queries;
    z3::params parameters(context);
    parameters.set("timeout", std::min(timeout_ms, 1000u));
    solver.set(parameters);
    const auto started = std::chrono::steady_clock::now();
    auto result = solver.check();
    if (result == z3::unknown) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (elapsed < timeout_ms) {
            ++coarse_lemma_retries;
            spdlog::debug("[coarse-path] lemma-augmented query returned unknown; "
                          "retrying without lemmas within the remaining budget");
            solver.reset();
            solver.add(original);
            parameters.set("timeout", timeout_ms - static_cast<unsigned>(elapsed));
            solver.set(parameters);
            result = solver.check();
        }
    }
    return result;
}

void VerificationSession::clear_module_state() {
    instruction_cache.clear();
    function_summary_cache = SymbolTable<FunctionSummary>();
    mathematical_helper_cache = SymbolTable<FunctionSummary>();
    state_transformer_cache.clear();
    loop_summary_cache = SymbolTable<LoopSummary>();
    cached_function_values.clear();
    solver_worker.reset();
    coarse_lemmas.reset();
    coarse_lemma_queries = 0;
    coarse_lemma_retries = 0;
    call_value_counters.clear();
    failed_loops.clear();
    memory_object_name_counters.clear();
    loop_path_alphabets.clear();
    nested_path_summary_cache.clear();
    nested_path_summaries_built = 0;
    next_path_symbol = 1;
    path_expression_id = 1;
    path_compressions = 0;
    path_accelerations = 0;
    path_affine_templates = 0;
    coarse_summary_attempts = 0;
    coarse_summaries = 0;
    coarse_refinements = 0;
    residue_attempts = residue_queries = residue_proofs = 0;
    coarse_degree_attempts.clear();
    analysis_manager.clear_module_state();
}
