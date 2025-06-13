#ifndef COMMON_H
#define COMMON_H

#include "llvm/Analysis/LoopInfo.h"

#include <string>
#include <algorithm>
#include "z3++.h"

namespace ari_exe {
    // Prefix for all Z3 variables
    constexpr const char* Z3_PREFIX = "ari_";

    inline std::string get_z3_name(const std::string& name) {
        auto z3_name = name;
        std::replace(z3_name.begin(), z3_name.end(), '.', '_');
        return Z3_PREFIX + z3_name;
    }

    /**
     * @brief Get all application of the given function in the expression
     */
    z3::expr_vector get_app_of(z3::expr e, z3::func_decl f);

    /**
     * @brief get the cartesian product
     * @param vec: a vector [i_1, ..., i_n] of integers, the cartesian product is [0, i_1 - 1] x ... x [0, i_n - 1]
     */
    std::vector<std::vector<int>>
    cartesian_product(const std::vector<int>& vec, int start = 0);

    /**
     * @brief check if the function can be flat in z3;
     *        Based on help_simplify() in z3,
     *        +,*,bvadd,bvmul,bvand,bvor,bvxor can be flat.
     */
    bool can_flat(const z3::func_decl& f);

    /**
     * @brief get the entering block of the given loop
     */
    llvm::BasicBlock* get_loop_entering_block(llvm::Loop* loop);
}

#endif