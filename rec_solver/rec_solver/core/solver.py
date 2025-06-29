from ..rec_parser import parse_str
from .recurrence import LoopRecurrence, MultiRecurrence
from .multivariate import solve_multivariate_rec
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from .poly_expr import poly_expr_solving

def solve_str(s):
    rec = parse_str(s)
    if isinstance(rec, LoopRecurrence):
        try:
            res = solve_ultimately_periodic_symbolic(rec)
            return res
        except:
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
