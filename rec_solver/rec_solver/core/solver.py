from ..rec_parser import parse_str
from .recurrence import LoopRecurrence, MultiRecurrence
from .multivariate import solve_multivariate_rec
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from .poly_expr import poly_expr_solving
from .bounded_cfinite import can_attempt_bounded_cfinite_map, solve_bounded_cfinite_map
from .solvable_polynomial import is_solvable_map

def solve_str(s):
    rec = parse_str(s)
    if isinstance(rec, LoopRecurrence):
        solvable = False
        try:
            solvable = is_solvable_map(rec)
        except:
            pass
        if not solvable and can_attempt_bounded_cfinite_map(rec, require_nonlinear=True):
            try:
                return solve_bounded_cfinite_map(rec)
            except:
                pass
        try:
            res = solve_ultimately_periodic_symbolic(rec)
            return res
        except:
            try:
                return solve_bounded_cfinite_map(rec)
            except:
                pass
            res = poly_expr_solving(rec, 2)
            if not res.is_trivial():
                return res
            return poly_expr_solving(rec, 3)
    elif isinstance(rec, MultiRecurrence):
        return solve_multivariate_rec(rec)
    return None

def solve_file(filename):
    with open(filename) as fp:
        return solve_str(fp.read())
