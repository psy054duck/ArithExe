#include "common.h"

using namespace ari_exe;

z3::expr_vector ari_exe::get_app_of(z3::expr e, z3::func_decl f) {
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