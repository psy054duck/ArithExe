#ifndef ARRAY_SHAPE_H
#define ARRAY_SHAPE_H

#include "state.h"

namespace ari_exe {
z3::expr_vector recover_array_shape(llvm::AllocaInst* allocation,
                                    const state_ptr& state, const z3::expr& count);
std::vector<Expression> add_coordinate_offset(
    const MemoryObject& array, const std::vector<Expression>& base,
    const Expression& offset);
std::vector<Expression> normalize_array_coordinates(
    const MemoryObject& array, const std::vector<Expression>& indices,
    const state_ptr& state);
void certify_array_coordinates(const MemoryObject& array,
                               const std::vector<Expression>& indices,
                               const state_ptr& state);
}

#endif
