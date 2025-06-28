#ifndef COMMON_H
#define COMMON_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/CallGraph.h"

#include <string>
#include <algorithm>
#include "z3++.h"

namespace ari_exe {
    enum VeriResult {
        HOLD,        // the verification condition is true
        FAIL,        // the verification condition is false
        VERIUNKNOWN, // the verification condition is unknown
    };

    enum TestResult {
        FEASIBLE,    // the state is feasible
        UNFEASIBLE,  // the state is unfeasible
        TESTUNKNOWN, // the state is unknown
    };

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

    /**
     * @brief Get SCC of the given function
     */
    std::set<llvm::Function*> get_SCC_for(llvm::CallGraph* CG, llvm::Function* F);

    /**
     * @brief Get all function applications in the given expression
     *        It is not a set, so elements may repeat.
     */
    std::vector<z3::expr> get_func_apps(z3::expr e);
}

#endif