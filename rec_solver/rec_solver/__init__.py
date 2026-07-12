from .core.ultimately_periodic import solve_ultimately_periodic_symbolic, solve_ultimately_periodic_initial
from .core.poly_expr import (
    find_cfinite_spaces,
    find_general_cfinite_spaces,
    find_nested_cfinite_spaces,
    find_special_cfinite_spaces,
    poly_expr_solving,
    solve_cfinite_rec,
)
from .core.multivariate import solve_multivariate_rec
from .rec_parser import parse_file, parse_str
from .core.multivariate import solve_multivariate_rec
from .core.solver import solve_file, solve_str
