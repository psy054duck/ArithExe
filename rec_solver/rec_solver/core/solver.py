from ..rec_parser import parse_str
from .recurrence import LoopRecurrence, MultiRecurrence
from .multivariate import solve_multivariate_rec
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from .poly_expr import poly_expr_solving
from .bounded_cfinite import can_attempt_bounded_cfinite_map, solve_bounded_cfinite_map
from .solvable_polynomial import is_solvable_map

def solve_str(
    s,
    enable_bounded_cfinite=True,
    poly_expr_strategy="auto",
    poly_expr_order=1,
    poly_expr_degree=None,
):
    rec = parse_str(s)
    if isinstance(rec, LoopRecurrence):
        solvable = False
        try:
            solvable = is_solvable_map(rec)
        except:
            pass
        if enable_bounded_cfinite and not solvable and can_attempt_bounded_cfinite_map(rec, require_nonlinear=True):
            try:
                return solve_bounded_cfinite_map(rec)
            except:
                pass
        try:
            res = solve_ultimately_periodic_symbolic(
                rec,
                enable_bounded_cfinite=enable_bounded_cfinite,
            )
            return res
        except:
            if enable_bounded_cfinite:
                try:
                    return solve_bounded_cfinite_map(rec)
                except:
                    pass
            degrees = [poly_expr_degree] if poly_expr_degree is not None else [2, 3]
            res = None
            for degree in degrees:
                res = poly_expr_solving(
                    rec,
                    degree,
                    strategy=poly_expr_strategy,
                    order=poly_expr_order,
                )
                if not res.is_trivial():
                    return res
            return res
    elif isinstance(rec, MultiRecurrence):
        return solve_multivariate_rec(rec)
    return None

def solve_file(
    filename,
    enable_bounded_cfinite=True,
    poly_expr_strategy="auto",
    poly_expr_order=1,
    poly_expr_degree=None,
):
    with open(filename) as fp:
        return solve_str(
            fp.read(),
            enable_bounded_cfinite=enable_bounded_cfinite,
            poly_expr_strategy=poly_expr_strategy,
            poly_expr_order=poly_expr_order,
            poly_expr_degree=poly_expr_degree,
        )
