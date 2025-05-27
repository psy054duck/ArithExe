#include "MemoryObject.h"

using namespace ari_exe;

std::optional<z3::expr>
MemoryObject::read() const {
    return scalar;
}

std::optional<z3::expr>
MemoryObject::read(z3::expr_vector index) const {
    // TODO: should add bounds checking here
    
    // read to an array object should reflect the writing traces
    auto& z3ctx = index.ctx();
    if (!func.has_value()) return std::nullopt;

    z3::expr res = func.value()(index);
    for (auto wr = write_traces.begin(); wr != write_traces.end(); ++wr) {
        z3::expr cond = z3ctx.bool_val(true);
        for (int i = 0; i < index.size(); ++i) {
            cond = cond && (wr->first[i] == index[i]);
        }
        res = z3::ite(cond, wr->second, res);
    }
    return res;
}

void
MemoryObject::write(z3::expr val) {
    scalar = val;
}

void
MemoryObject::write(z3::expr_vector index, z3::expr val) {
    write_traces.push_front(std::make_pair(index, val));
}

std::pair<z3::expr, z3::expr_vector>
MemoryObject::get_array_value() const {
    assert(func.has_value() && "MemoryObject is not an array, cannot get array value");
    auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
    z3::expr_vector indices(z3ctx);
    std::string name_prefix = func.value().name().str() + "_index_";
    for (int i = 0; i < get_dims().size(); ++i) {
        auto name = name_prefix + std::to_string(i);
        indices.push_back(z3ctx.int_const(name.c_str()));
    }
    return {read(indices).value(), indices};
}
