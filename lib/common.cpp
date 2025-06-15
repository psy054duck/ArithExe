#include "common.h"

namespace ari_exe {
    z3::expr_vector get_app_of(z3::expr e, z3::func_decl f) {
        z3::expr_vector result(e.ctx());
        if (e.is_app() && e.decl().id() == f.id()) {
            result.push_back(e);
        }
        for (unsigned i = 0; i < e.num_args(); ++i) {
            auto sub_exprs = get_app_of(e.arg(i), f);
            for (const auto& sub_expr : sub_exprs) result.push_back(sub_expr);
        }
        return result;
    }

    std::vector<std::vector<int>>
    cartesian_product(const std::vector<int>& vec, int start) {
        assert(vec.size() > 0);
        auto res = std::vector<std::vector<int>>();
        if (start == vec.size() - 1) {
            for (int i = 0; i < vec[start]; i++) {
                res.push_back({i});
            }
            return res;
        }
        auto sub_res = cartesian_product(vec, start + 1);
        for (int i = 0; i < vec[start]; i++) {
            for (auto& sub : sub_res) {
                std::vector<int> tmp = {i};
                tmp.insert(tmp.end(), sub.begin(), sub.end());
                res.push_back(tmp);
            }
        }
        return res;
    }

    bool can_flat(const z3::func_decl& f) {
        auto kind = f.decl_kind();
        return kind == Z3_OP_ADD || kind == Z3_OP_MUL || kind == Z3_OP_BADD ||
               kind == Z3_OP_BMUL || kind == Z3_OP_BAND || kind == Z3_OP_BOR ||
               kind == Z3_OP_BXOR;
    }

    llvm::BasicBlock*
    get_loop_entering_block(llvm::Loop* loop) {
        for (auto& inst : *loop->getHeader()) {
            if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(&inst)) {
                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                    auto incoming_block = phi->getIncomingBlock(i);
                    if (!loop->contains(incoming_block)) {
                        // This is the block that enters the loop
                        return incoming_block;
                    }
                }
            }
        }
        return nullptr;
    }

    std::set<llvm::Function*>
    get_SCC_for(llvm::CallGraph* CG, llvm::Function* F) {
        std::set<llvm::Function*> SCC;
        for (auto scc = llvm::scc_begin(CG); scc != llvm::scc_end(CG); ++scc) {
            auto &node = *scc;
            auto it = std::find_if(node.begin(), node.end(), [F](llvm::CallGraphNode* n) {
                                    return n->getFunction() == F;
                                });
            if (it != node.end()) {
                for (auto it = node.begin(); it != node.end(); ++it) {
                        SCC.insert((*it)->getFunction());
                }
                break;
            }
        }
        return SCC;
    }

    std::vector<z3::expr>
    get_func_apps(z3::expr e) {
        std::vector<z3::expr> result;
        if (e.is_const() || e.is_algebraic()) {
            return result;
        } else if (e.is_app() && !e.is_const() && e.decl().decl_kind() == Z3_OP_UNINTERPRETED) {
            result.push_back(e);
        } else {
            for (unsigned i = 0; i < e.num_args(); ++i) {
                auto sub_exprs = get_func_apps(e.arg(i));
                for (const auto& sub_expr : sub_exprs) {
                    result.push_back(sub_expr);
                }
            }
        }
        return result;
    }
}