#include "EntryArray.h"

#include <isl/ctx.h>
#include <isl/options.h>
#include <isl/set.h>
#include <optional>
#include <functional>
#include <set>
#include <sstream>
#include <spdlog/spdlog.h>
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"

namespace ari_exe {
namespace {

class AffineAccesses {
  public:
    explicit AffineAccesses(llvm::Loop* loop): loop(loop),
        layout(loop->getHeader()->getModule()->getDataLayout()) {}

    std::optional<std::string> integer(llvm::Value* value,
                                        const std::string& time) {
        if (auto* constant = llvm::dyn_cast<llvm::ConstantInt>(value)) {
            llvm::SmallString<32> text;
            constant->getValue().toString(text, 10, true);
            return std::string(text);
        }
        if (!value->getType()->isIntegerTy() || value->getType()->isIntegerTy(1))
            return std::nullopt;
        auto* inst = llvm::dyn_cast<llvm::Instruction>(value);
        if (!inst || !loop->contains(inst)) {
            auto [it, inserted] = parameters.try_emplace(value, "p" + std::to_string(parameters.size()));
            return it->second;
        }
        if (!active.insert(value).second) return std::nullopt;
        auto result = local_integer(inst, time);
        active.erase(value);
        return result;
    }

    std::optional<std::string> offset(llvm::Value* pointer,
                                       const std::string& time) {
        auto* gep = llvm::dyn_cast<llvm::GEPOperator>(pointer);
        if (!gep) {
            if (auto* cast = llvm::dyn_cast<llvm::BitCastOperator>(pointer))
                return offset(cast->getOperand(0), time);
            if (auto* inst = llvm::dyn_cast<llvm::Instruction>(pointer);
                inst && loop->contains(inst)) return std::nullopt;
            return "0";
        }
        auto base = offset(gep->getPointerOperand(), time);
        if (!base) return std::nullopt;
        std::string result = *base;
        for (auto it = llvm::gep_type_begin(gep), end = llvm::gep_type_end(gep);
             it != end; ++it) {
            if (it.isStruct() || it.isVector()) return std::nullopt;
            auto size = layout.getTypeAllocSize(it.getIndexedType());
            auto index = integer(it.getOperand(), time);
            if (size.isScalable() || !index) return std::nullopt;
            result = "(" + result + "+" + std::to_string(size.getFixedValue()) + "*(" + *index + "))";
        }
        return result;
    }

    std::string prefix() const {
        if (parameters.empty()) return "";
        std::string result = "[";
        for (unsigned i = 0; i < parameters.size(); ++i) {
            if (i) result += ",";
            result += "p" + std::to_string(i);
        }
        return result + "] -> ";
    }

  private:
    std::optional<std::string> local_integer(llvm::Instruction* inst,
                                              const std::string& time) {
        if (auto* cast = llvm::dyn_cast<llvm::CastInst>(inst)) {
            // These are identities only under the explicit integer relaxation.
            if (cast->getOpcode() == llvm::Instruction::SExt ||
                cast->getOpcode() == llvm::Instruction::ZExt ||
                cast->getOpcode() == llvm::Instruction::Trunc)
                return integer(cast->getOperand(0), time);
            return std::nullopt;
        }
        if (auto* phi = llvm::dyn_cast<llvm::PHINode>(inst)) {
            if (phi->getParent() != loop->getHeader()) return std::nullopt;
            llvm::Value* start = nullptr;
            std::optional<std::string> step;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                auto* incoming = phi->getIncomingValue(i);
                if (!loop->contains(phi->getIncomingBlock(i))) {
                    if (start) return std::nullopt;
                    start = incoming;
                    continue;
                }
                auto* op = llvm::dyn_cast<llvm::BinaryOperator>(incoming);
                if (!op) return std::nullopt;
                auto* constant = llvm::dyn_cast<llvm::ConstantInt>(op->getOperand(1));
                bool negative = op->getOpcode() == llvm::Instruction::Sub;
                if ((!negative && op->getOpcode() != llvm::Instruction::Add) ||
                    op->getOperand(0) != phi || !constant) return std::nullopt;
                auto increment = constant->getValue().sext(constant->getBitWidth() + 1);
                if (negative) increment = -increment;
                llvm::SmallString<32> text;
                increment.toString(text, 10, true);
                std::string current(text);
                if (step && *step != current) return std::nullopt;
                step = current;
            }
            if (!start || !step) return std::nullopt;
            auto initial = integer(start, time);
            if (!initial) return std::nullopt;
            return "(" + *initial + "+" + *step + "*" + time + ")";
        }
        auto* op = llvm::dyn_cast<llvm::BinaryOperator>(inst);
        if (!op) return std::nullopt;
        const auto code = op->getOpcode();
        if (code != llvm::Instruction::Add && code != llvm::Instruction::Sub &&
            code != llvm::Instruction::Mul) return std::nullopt;
        if (code == llvm::Instruction::Mul &&
            !llvm::isa<llvm::ConstantInt>(op->getOperand(0)) &&
            !llvm::isa<llvm::ConstantInt>(op->getOperand(1))) return std::nullopt;
        auto lhs = integer(op->getOperand(0), time);
        auto rhs = integer(op->getOperand(1), time);
        if (!lhs || !rhs) return std::nullopt;
        const char* symbol = code == llvm::Instruction::Add ? "+" :
                             code == llvm::Instruction::Sub ? "-" : "*";
        return "(" + *lhs + symbol + *rhs + ")";
    }

    llvm::Loop* loop;
    const llvm::DataLayout& layout;
    std::map<llvm::Value*, std::string> parameters;
    std::set<llvm::Value*> active;
};

MemoryObjectPtr object_at(llvm::Value* pointer, const state_ptr& entry) {
    auto* base = llvm::getUnderlyingObject(pointer);
    auto object = entry->memory.get_object(base);
    if (object && object->is_pointer())
        return entry->memory.get_object(object->get_ptr_value());
    return object;
}

bool may_precede(llvm::Instruction* write, llvm::Instruction* read,
                 llvm::Loop* loop) {
    if (write->getParent() == read->getParent()) return write->comesBefore(read);
    std::vector<llvm::BasicBlock*> work;
    std::set<llvm::BasicBlock*> seen;
    for (auto* block : llvm::successors(write->getParent())) work.push_back(block);
    while (!work.empty()) {
        auto* block = work.back();
        work.pop_back();
        if (!loop->contains(block) || block == loop->getHeader() ||
            !seen.insert(block).second) continue;
        if (block == read->getParent()) return true;
        for (auto* next : llvm::successors(block)) work.push_back(next);
    }
    return false;
}

} // namespace

bool reads_entry_array(llvm::Loop* loop, llvm::LoadInst* read,
                       const state_ptr& entry) {
    if (!entry->session.uses_integer_relaxation() || !loop->getSubLoops().empty() ||
        read->isVolatile() || read->isAtomic()) return false;
    // Removing backedges to the header must leave an acyclic iteration.
    std::set<llvm::BasicBlock*> active, done;
    std::function<bool(llvm::BasicBlock*)> acyclic = [&](llvm::BasicBlock* block) {
        if (done.contains(block)) return true;
        if (!active.insert(block).second) return false;
        for (auto* next : llvm::successors(block)) {
            if (loop->contains(next) && next != loop->getHeader() && !acyclic(next))
                return false;
        }
        active.erase(block);
        done.insert(block);
        return true;
    };
    for (auto* block : loop->blocks()) if (!acyclic(block)) return false;
    auto read_object = object_at(read->getPointerOperand(), entry);
    if (!read_object) return false;
    AffineAccesses accesses(loop);
    std::optional<std::string> read_offset;
    const auto& layout = read->getModule()->getDataLayout();
    auto read_size = layout.getTypeStoreSize(read->getType());
    if (read_size.isScalable()) return false;
    for (auto* block : loop->blocks()) {
        for (auto& instruction : *block) {
            if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
                if (!call->onlyReadsMemory()) return false;
            }
            if (llvm::isa<llvm::AtomicRMWInst>(instruction) ||
                llvm::isa<llvm::AtomicCmpXchgInst>(instruction)) return false;
            auto* write = llvm::dyn_cast<llvm::StoreInst>(&instruction);
            if (!write) continue;
            if (write->isVolatile() || write->isAtomic()) return false;
            auto write_object = object_at(write->getPointerOperand(), entry);
            if (!write_object) return false;
            if (write_object != read_object) continue;
            if (llvm::getUnderlyingObject(write->getPointerOperand()) !=
                llvm::getUnderlyingObject(read->getPointerOperand())) return false;
            // Affine offsets are needed only for potentially aliasing writes.
            // A read-only object remains an entry snapshot at any address.
            if (!read_offset) read_offset = accesses.offset(read->getPointerOperand(), "tr");
            if (!read_offset) return false;
            auto write_offset = accesses.offset(write->getPointerOperand(), "tw");
            auto write_size = layout.getTypeStoreSize(write->getValueOperand()->getType());
            if (!write_offset || write_size.isScalable()) return false;
            // Ignore data-dependent branch guards and loop bounds: this is a
            // conservative may-dependence relation over all nonnegative times.
            const auto order = may_precede(write, read, loop) ? "tw <= tr" : "tw < tr";
            std::string relation = accesses.prefix() + "{ [tw,tr] : tw>=0 and tr>=0 and " +
                order + " and " + *write_offset + " < " + *read_offset + "+" +
                std::to_string(read_size.getFixedValue()) + " and " + *read_offset + " < " +
                *write_offset + "+" + std::to_string(write_size.getFixedValue()) + " }";
            isl_ctx* ctx = isl_ctx_alloc();
            isl_options_set_on_error(ctx, ISL_ON_ERROR_CONTINUE);
            isl_ctx_set_max_operations(ctx, 100000);
            isl_set* dependence = isl_set_read_from_str(ctx, relation.c_str());
            const bool empty = dependence && isl_set_is_empty(dependence) == isl_bool_true;
            isl_set_free(dependence);
            isl_ctx_free(ctx);
            spdlog::debug("[poly-entry] dependence {}: {}", relation, empty ? "empty" : "possible");
            if (!empty) return false;
        }
    }
    return true;
}

} // namespace ari_exe
