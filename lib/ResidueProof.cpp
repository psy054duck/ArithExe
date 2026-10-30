#include "ResidueProof.h"
#include "VerificationSession.h"
#include "IntegerSemantics.h"
#include "UnobservedGlobals.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace ari_exe {
namespace {
using Values = std::map<llvm::Value*, z3::expr>;
using Live = std::set<llvm::Value*>;
struct Inconclusive : std::runtime_error {
    using std::runtime_error::runtime_error;
};

unsigned limit(const char* name, unsigned fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw || *raw == '-') return fallback;
    char* end = nullptr;
    const auto value = std::strtoul(raw, &end, 10);
    return end && !*end && value <= 1000000 ? value : fallback;
}

bool nondet(llvm::Function* function) {
    if (!function || !function->isDeclaration()) return false;
    const auto name = function->getName();
    return name == "__VERIFIER_nondet_bool" || name == "__VERIFIER_nondet_int" ||
        name == "__VERIFIER_nondet_uint" || name == "__VERIFIER_nondet_char" ||
        name == "__VERIFIER_nondet_schar" || name == "__VERIFIER_nondet_uchar" ||
        name == "__VERIFIER_nondet_short" || name == "__VERIFIER_nondet_ushort" ||
        name == "__VERIFIER_nondet_long" || name == "__VERIFIER_nondet_ulong" ||
        name == "__VERIFIER_nondet_longlong" || name == "__VERIFIER_nondet_ulonglong";
}

llvm::Function* callee(llvm::CallInst* call) {
    return llvm::dyn_cast<llvm::Function>(call->getCalledOperand()->stripPointerCasts());
}

bool error_name(llvm::StringRef name) {
    return name == "reach_error" || name == "__assert_fail" ||
           name == "__VERIFIER_error";
}

struct Feature {
    llvm::Value* value;
    unsigned modulus; // 0 denotes the exhaustive zero/nonzero partition.
};
struct Cell {
    llvm::BasicBlock* block;
    std::vector<unsigned> classes;
};

class Proof {
    llvm::Function* function;
    VerificationSession& session;
    z3::context& ctx;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    unsigned timeout = limit("ARITHEXE_RESIDUE_TIMEOUT_MS", 3000);
    unsigned state_limit = limit("ARITHEXE_RESIDUE_MAX_STATES", 2048);
    unsigned query_limit = limit("ARITHEXE_RESIDUE_MAX_QUERIES", 8192);
    unsigned queries = 0, cells = 0, calls = 0, obligations = 0;
    std::set<llvm::GlobalVariable*> dead_globals, readonly_globals;
    std::map<llvm::Value*, std::set<unsigned>> residues;
    Live zero_values;
    std::map<llvm::Value*, unsigned> numbering;
    std::map<llvm::BasicBlock*, std::vector<llvm::Value*>> coordinates;
    std::map<llvm::BasicBlock*, std::vector<Feature>> features;
    std::map<llvm::BasicBlock*, std::set<std::vector<unsigned>>> reached;
    std::deque<Cell> pending;
    std::set<llvm::BasicBlock*> cutpoints;
    std::set<llvm::BasicBlock*> active;

    void budget() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (elapsed >= timeout || queries >= query_limit || cells >= state_limit)
            throw Inconclusive("residue proof budget exhausted");
    }
    z3::check_result check(const z3::expr& formula) {
        budget();
        ++queries;
        session.note_residue_query();
        z3::solver solver(ctx);
        z3::params options(ctx);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        // Z3 timeout=0 means unlimited, so never let a deadline crossed
        // between budget() and this measurement become a zero timeout.
        if (elapsed >= timeout) throw Inconclusive("residue proof budget exhausted");
        options.set("timeout", std::min(200u, timeout - static_cast<unsigned>(elapsed)));
        solver.set(options);
        solver.add(formula.simplify());
        auto result = solver.check();
        if (result == z3::unknown) throw Inconclusive("residue query returned UNKNOWN");
        return result;
    }
    unsigned width(llvm::Value* value) {
        if (!value->getType()->isIntegerTy()) throw Inconclusive("non-integer scalar");
        auto w = value->getType()->getIntegerBitWidth();
        if (w > 64) throw Inconclusive("scalar wider than 64 bits");
        return w;
    }
    z3::expr modulus(llvm::Value* value) {
        return integer_semantics::power_of_two(ctx, width(value));
    }
    z3::expr signed_value(const z3::expr& value, llvm::Value* source) {
        auto m = modulus(source);
        return z3::ite(value >= m / 2, value - m, value);
    }
    z3::expr value(llvm::Value* source, const Values& values) {
        if (auto* constant = llvm::dyn_cast<llvm::ConstantInt>(source)) {
            llvm::SmallString<64> text;
            constant->getValue().toString(text, 10, false);
            return ctx.int_val(text.c_str());
        }
        auto found = values.find(source);
        if (found == values.end()) throw Inconclusive("missing live-in scalar");
        return found->second;
    }
    z3::expr cell_predicate(const Feature& feature, unsigned choice, const z3::expr& expr) {
        if (feature.modulus) return expr % static_cast<int>(feature.modulus) == static_cast<int>(choice);
        return choice ? expr != 0 : expr == 0;
    }
    bool ignored(llvm::Instruction& instruction) {
        if (instruction.isDebugOrPseudoInst()) return true;
        auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
        return store && dead_globals.contains(
            llvm::dyn_cast<llvm::GlobalVariable>(store->getPointerOperand()));
    }
    void classify_globals() {
        for (auto* global : unobserved_store_globals(*function->getParent()))
            dead_globals.insert(global);
        for (auto& global : function->getParent()->globals()) {
            bool all_loads = true;
            for (auto* user : global.users()) {
                auto* load = llvm::dyn_cast<llvm::LoadInst>(user);
                all_loads = all_loads && load && load->getPointerOperand() == &global &&
                    !load->isVolatile() && !load->isAtomic();
            }
            if (all_loads && global.hasInitializer() &&
                llvm::isa<llvm::ConstantInt>(global.getInitializer()))
                readonly_globals.insert(&global);
        }
    }
    void mark(llvm::Value* source, unsigned divisor) {
        if (llvm::isa<llvm::Constant>(source) || !source->getType()->isIntegerTy()) return;
        if (divisor ? !residues[source].insert(divisor).second : !zero_values.insert(source).second) return;
        auto* instruction = llvm::dyn_cast<llvm::Instruction>(source);
        if (!instruction) return;
        // Propagate through the SSA copy/update web, not through an arbitrary
        // call's arguments. This creates predicates, never assumed equalities.
        if (llvm::isa<llvm::PHINode>(instruction) || llvm::isa<llvm::CastInst>(instruction) ||
            (divisor && llvm::isa<llvm::BinaryOperator>(instruction))) {
            for (auto& operand : instruction->operands()) mark(operand.get(), divisor);
        } else if (auto* selection = llvm::dyn_cast<llvm::SelectInst>(instruction)) {
            mark(selection->getTrueValue(), divisor);
            mark(selection->getFalseValue(), divisor);
        }
    }
    bool initialize() {
        if (!function->arg_empty()) return false;
        auto& loops = session.analyses().get_LI(function);
        cutpoints.insert(&function->getEntryBlock());
        for (auto& block : *function)
            if (auto* loop = loops.getLoopFor(&block); loop && loop->getHeader() == &block)
                cutpoints.insert(&block);
        bool modular = false, fresh_loop = false;
        classify_globals();
        unsigned index = 0;
        for (auto& block : *function) for (auto& instruction : block) {
            if (ignored(instruction)) continue;
            numbering.emplace(&instruction, index++);
            if (auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction)) {
                if (loops.getLoopFor(&block) && nondet(callee(call))) fresh_loop = true;
            }
            if (instruction.getOpcode() == llvm::Instruction::URem) {
                auto* divisor = llvm::dyn_cast<llvm::ConstantInt>(instruction.getOperand(1));
                if (!divisor || divisor->getValue().getActiveBits() > 5) return false;
                unsigned d = divisor->getZExtValue();
                if (d < 2 || d > 16) return false;
                // Automatic fallback targets non-power-of-two modular control,
                // leaving the existing deterministic parity accelerator alone.
                if ((d & (d - 1)) != 0) modular = true;
                mark(instruction.getOperand(0), d);
            }
            if (auto* comparison = llvm::dyn_cast<llvm::ICmpInst>(&instruction)) {
                if (!comparison->isEquality()) continue;
                for (unsigned i = 0; i < 2; ++i)
                    if (auto* constant = llvm::dyn_cast<llvm::ConstantInt>(comparison->getOperand(i));
                        constant && constant->isZero()) mark(comparison->getOperand(1 - i), 0);
            }
        }
        if (!modular || !fresh_loop) {
            spdlog::debug("[residue-path] profile not selected: nonbinary modulus={}, fresh loop={}", modular, fresh_loop);
            return false;
        }

        std::map<llvm::BasicBlock*, Live> uses, defs, in, out;
        for (auto& block : *function) for (auto& instruction : block) {
            if (ignored(instruction)) continue;
            if (instruction.getType()->isIntegerTy()) defs[&block].insert(&instruction);
            if (llvm::isa<llvm::PHINode>(instruction)) continue;
            for (auto& operand : instruction.operands()) {
                auto* definition = llvm::dyn_cast<llvm::Instruction>(operand.get());
                if (definition && definition->getType()->isIntegerTy() &&
                    definition->getParent() != &block) uses[&block].insert(definition);
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& block : *function) {
                Live next_out;
                for (auto* successor : llvm::successors(&block)) {
                    next_out.insert(in[successor].begin(), in[successor].end());
                    for (auto& phi : successor->phis()) {
                        auto* incoming = llvm::dyn_cast<llvm::Instruction>(phi.getIncomingValueForBlock(&block));
                        if (incoming && incoming->getType()->isIntegerTy()) next_out.insert(incoming);
                    }
                }
                Live next_in = uses[&block];
                for (auto* item : next_out) if (!defs[&block].contains(item)) next_in.insert(item);
                changed = changed || next_in != in[&block] || next_out != out[&block];
                in[&block] = std::move(next_in); out[&block] = std::move(next_out);
            }
        }
        for (auto& block : *function) {
            Live slots = in[&block];
            for (auto& phi : block.phis()) slots.insert(&phi);
            auto& list = coordinates[&block];
            list.assign(slots.begin(), slots.end());
            std::sort(list.begin(), list.end(), [&](auto* a, auto* b) { return numbering.at(a) < numbering.at(b); });
            for (auto* item : list) {
                width(item);
                for (auto d : residues[item]) features[&block].push_back({item, d});
                if (zero_values.contains(item) || width(item) == 1)
                    features[&block].push_back({item, 0});
            }
        }
        if (!coordinates[&function->getEntryBlock()].empty()) {
            for (auto* item : coordinates[&function->getEntryBlock()])
                spdlog::debug("[residue-path] unsupported entry live-in {}", item->getName().str());
            return false;
        }
        spdlog::debug("[residue-path] independent guarded CFG proof; {} store-only globals ignored",
                      dead_globals.size());
        return true;
    }

    void enqueue(llvm::BasicBlock* block, const Values& incoming, const z3::expr& path) {
        std::vector<unsigned> classes;
        const auto& partition = features[block];
        std::function<void(unsigned, const z3::expr&)> project = [&](unsigned i, const z3::expr& condition) {
            if (check(condition) == z3::unsat) return;
            if (i == partition.size()) {
                if (reached[block].insert(classes).second) {
                    budget(); ++cells;
                    pending.push_back({block, classes});
                }
                return;
            }
            const auto& feature = partition[i];
            for (unsigned r = 0; r < (feature.modulus ? feature.modulus : 2); ++r) {
                classes.push_back(r);
                project(i + 1, condition && cell_predicate(feature, r, value(feature.value, incoming)));
                classes.pop_back();
            }
        };
        project(0, path);
    }
    void edge(llvm::BasicBlock* source, llvm::BasicBlock* target, const Values& values, const z3::expr& path) {
        if (check(path) == z3::unsat) return;
        Values incoming;
        for (auto* slot : coordinates[target]) {
            auto* phi = llvm::dyn_cast<llvm::PHINode>(slot);
            auto* rhs = phi && phi->getParent() == target ? phi->getIncomingValueForBlock(source) : slot;
            if (!rhs) throw Inconclusive("missing PHI predecessor edge");
            incoming.emplace(slot, value(rhs, values));
        }
        // All PHI RHSs above read the unchanged predecessor map.
        if (cutpoints.contains(target)) enqueue(target, incoming, path);
        else execute_block(target, std::move(incoming), path);
    }
    void safety(const z3::expr& path, const z3::expr& assertion) {
        ++obligations;
        if (check(path && !assertion) != z3::unsat)
            throw Inconclusive("abstract error/assertion remains feasible");
    }
    void execute(const Cell& cell) {
        Values values;
        z3::expr path = ctx.bool_val(true);
        for (auto* slot : coordinates[cell.block]) {
            auto name = "ari_residue_v_" + std::to_string(numbering.at(slot));
            auto symbol = ctx.int_const(name.c_str());
            values.emplace(slot, symbol);
            path = path && symbol >= 0 && symbol < modulus(slot);
        }
        for (unsigned i = 0; i < cell.classes.size(); ++i)
            path = path && cell_predicate(features[cell.block][i], cell.classes[i],
                                          value(features[cell.block][i].value, values));
        execute_block(cell.block, std::move(values), path);
    }
    void execute_block(llvm::BasicBlock* block, Values values, z3::expr path) {
        if (active.size() >= 512 || !active.insert(block).second)
            throw Inconclusive("unsupported irreducible CFG cycle or excessive path depth");
        struct Scope {
            std::set<llvm::BasicBlock*>& active;
            llvm::BasicBlock* block;
            ~Scope() { active.erase(block); }
        } scope{active, block};
        // Preserve exact guard/update relationships through acyclic paths.
        // Abstract only at cycle cutpoints; independently abstracting a saved
        // remainder in an intermediate block would lose r == x % d.
        for (auto& instruction : *block) {
            budget();
            if (ignored(instruction) || llvm::isa<llvm::PHINode>(instruction)) continue;
            if (auto* branch = llvm::dyn_cast<llvm::BranchInst>(&instruction)) {
                if (branch->isUnconditional()) edge(block, branch->getSuccessor(0), values, path);
                else {
                    auto condition = value(branch->getCondition(), values) != 0;
                    edge(block, branch->getSuccessor(0), values, path && condition);
                    edge(block, branch->getSuccessor(1), values, path && !condition);
                }
                return;
            }
            if (llvm::isa<llvm::ReturnInst>(instruction) || llvm::isa<llvm::UnreachableInst>(instruction)) return;
            if (auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction)) {
                auto* target = callee(call);
                if (!target) throw Inconclusive("indirect call");
                auto name = target->getName();
                if (error_name(name)) { safety(path, ctx.bool_val(false)); return; }
                if (name == "abort" && target->isDeclaration()) return;
                if (name == "__VERIFIER_assert" && call->arg_size() == 1) {
                    auto assertion = value(call->getArgOperand(0), values) != 0;
                    safety(path, assertion); path = path && assertion; continue;
                }
                if (name == "__VERIFIER_assume" && call->arg_size() == 1) {
                    path = path && value(call->getArgOperand(0), values) != 0; continue;
                }
                if (!nondet(target) || !call->arg_empty()) throw Inconclusive("unsupported call");
                auto symbol = ctx.int_const(("ari_residue_call_" + std::to_string(calls++)).c_str());
                values.emplace(call, symbol);
                path = path && symbol >= 0 && symbol < modulus(call);
                if (name == "__VERIFIER_nondet_bool") path = path && symbol <= 1;
                continue;
            }
            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
                auto* global = llvm::dyn_cast<llvm::GlobalVariable>(load->getPointerOperand());
                if (!readonly_globals.contains(global) ||
                    global->getInitializer()->getType() != load->getType())
                    throw Inconclusive("mutable, escaping or reinterpreted memory");
                values.emplace(load, value(global->getInitializer(), values)); continue;
            }
            if (auto* comparison = llvm::dyn_cast<llvm::ICmpInst>(&instruction)) {
                auto* left = comparison->getOperand(0); auto* right = comparison->getOperand(1);
                auto a = value(left, values), b = value(right, values);
                if (comparison->isSigned()) { a = signed_value(a, left); b = signed_value(b, right); }
                z3::expr predicate(ctx);
                switch (comparison->getPredicate()) {
                    case llvm::ICmpInst::ICMP_EQ: predicate = a == b; break;
                    case llvm::ICmpInst::ICMP_NE: predicate = a != b; break;
                    case llvm::ICmpInst::ICMP_UGT: case llvm::ICmpInst::ICMP_SGT: predicate = a > b; break;
                    case llvm::ICmpInst::ICMP_UGE: case llvm::ICmpInst::ICMP_SGE: predicate = a >= b; break;
                    case llvm::ICmpInst::ICMP_ULT: case llvm::ICmpInst::ICMP_SLT: predicate = a < b; break;
                    case llvm::ICmpInst::ICMP_ULE: case llvm::ICmpInst::ICMP_SLE: predicate = a <= b; break;
                    default: throw Inconclusive("unsupported comparison");
                }
                values.emplace(comparison, z3::ite(predicate, ctx.int_val(1), ctx.int_val(0))); continue;
            }
            if (auto* selection = llvm::dyn_cast<llvm::SelectInst>(&instruction)) {
                values.emplace(selection, z3::ite(value(selection->getCondition(), values) != 0,
                    value(selection->getTrueValue(), values), value(selection->getFalseValue(), values)));
                continue;
            }
            if (auto* cast = llvm::dyn_cast<llvm::CastInst>(&instruction)) {
                auto* source = cast->getOperand(0);
                auto expr = value(source, values);
                if (cast->getOpcode() == llvm::Instruction::SExt) {
                    expr = signed_value(expr, source);
                    expr = z3::ite(expr < 0, expr + modulus(cast), expr);
                } else if (cast->getOpcode() == llvm::Instruction::Trunc) expr = expr % modulus(cast);
                else if (cast->getOpcode() != llvm::Instruction::ZExt) throw Inconclusive("unsupported cast");
                values.emplace(cast, expr); continue;
            }
            if (auto* binary = llvm::dyn_cast<llvm::BinaryOperator>(&instruction)) {
                auto* left = binary->getOperand(0); auto* right = binary->getOperand(1);
                auto a = value(left, values), b = value(right, values), m = modulus(binary);
                z3::expr result(ctx);
                auto opcode = binary->getOpcode();
                if (opcode == llvm::Instruction::Add || opcode == llvm::Instruction::Sub || opcode == llvm::Instruction::Mul) {
                    auto raw = opcode == llvm::Instruction::Add ? a + b : opcode == llvm::Instruction::Sub ? a - b : a * b;
                    if (binary->hasNoUnsignedWrap()) safety(path, raw >= 0 && raw < m);
                    if (binary->hasNoSignedWrap()) {
                        auto sa = signed_value(a, left), sb = signed_value(b, right);
                        auto sr = opcode == llvm::Instruction::Add ? sa + sb : opcode == llvm::Instruction::Sub ? sa - sb : sa * sb;
                        // Require definedness, rather than silently removing
                        // executions with unused poison from the abstraction.
                        safety(path, sr >= -m / 2 && sr < m / 2);
                    }
                    result = opcode == llvm::Instruction::Add ? z3::ite(raw >= m, raw - m, raw) :
                             opcode == llvm::Instruction::Sub ? z3::ite(raw < 0, raw + m, raw) : raw % m;
                } else if (opcode == llvm::Instruction::URem || opcode == llvm::Instruction::UDiv) {
                    safety(path, b != 0);
                    if (binary->isExact() && opcode == llvm::Instruction::UDiv) safety(path, a % b == 0);
                    result = opcode == llvm::Instruction::URem ? a % b : a / b;
                } else if (width(binary) == 1 && (opcode == llvm::Instruction::And || opcode == llvm::Instruction::Or || opcode == llvm::Instruction::Xor)) {
                    auto predicate = opcode == llvm::Instruction::And ? (a != 0 && b != 0) :
                                     opcode == llvm::Instruction::Or ? (a != 0 || b != 0) : (a != b);
                    result = z3::ite(predicate, ctx.int_val(1), ctx.int_val(0));
                } else throw Inconclusive("unsupported scalar operation");
                values.emplace(binary, result.simplify()); continue;
            }
            throw Inconclusive("unsupported instruction or memory effect");
        }
        throw Inconclusive("missing basic-block terminator");
    }
public:
    Proof(llvm::Function* function, VerificationSession& session)
        : function(function), session(session), ctx(session.analyses().get_z3ctx()) {}
    bool run() {
        if (!initialize()) return false;
        session.note_residue_attempt();
        try {
            pending.push_back({&function->getEntryBlock(), {}});
            reached[&function->getEntryBlock()].insert({});
            while (!pending.empty()) {
                auto cell = pending.front(); pending.pop_front();
                execute(cell);
            }
            if (!obligations) return false;
            session.note_residue_proof();
            spdlog::info("[residue-path] proved guarded CFG fixed point: {} cell(s), {} query/obligation check(s), {} obligation(s)", cells + 1, queries, obligations);
            return true;
        } catch (const Inconclusive& error) {
            spdlog::debug("[residue-path] inconclusive after {} cells and {} queries: {}; retaining ordinary execution", cells, queries, error.what());
            return false;
        }
    }
};
}

bool prove_guarded_residues(llvm::Function* function, VerificationSession& session) {
    const char* coarse = std::getenv("ARITHEXE_COARSE_FALLBACK");
    const char* enabled = std::getenv("ARITHEXE_RESIDUE_FALLBACK");
    if ((coarse && std::string(coarse) == "0") || (enabled && std::string(enabled) == "0")) return false;
    try { return Proof(function, session).run(); }
    catch (const std::exception& error) {
        spdlog::debug("[residue-path] unsupported proof profile: {}; retaining ordinary execution", error.what());
        return false;
    }
}
}
