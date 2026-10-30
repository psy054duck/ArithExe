import z3
# Keep this eager import: changing the solver's import order causes severe
# performance regressions in conditional array recurrences.
import fire
import os
import sys
from functools import reduce
from sympy.core.function import AppliedUndef
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'rec_solver'))
from rec_solver import solve_str
from rec_solver.core.closed_form import MultiFuncClosedForm, ExprClosedForm, SymbolicClosedForm, PiecewiseClosedForm
from rec_solver.core.utils import to_z3, get_applied_functions

def env_flag(name, default=True):
    raw_value = os.environ.get(name)
    if raw_value is None:
        return default
    return raw_value.strip().lower() not in {"0", "false", "no", "off", "disabled"}

def env_int(name, default):
    raw_value = os.environ.get(name)
    if raw_value is None:
        return default
    try:
        return int(raw_value)
    except ValueError:
        return default

def env_optional_int(name):
    raw_value = os.environ.get(name)
    if raw_value is None or raw_value.strip() == "":
        return None
    try:
        return int(raw_value)
    except ValueError:
        return None

def solver_options():
    return dict(
        periodic_strategy=os.environ.get("ARITHEXE_PERIODIC_STRATEGY", "compression"),
        enable_bounded_cfinite=env_flag("ARITHEXE_ENABLE_BOUNDED_CFINITE"),
        poly_expr_strategy=os.environ.get("ARITHEXE_POLY_EXPR_STRATEGY", "auto"),
        poly_expr_order=env_int("ARITHEXE_POLY_EXPR_ORDER", 1),
        poly_expr_degree=env_optional_int("ARITHEXE_POLY_EXPR_DEGREE"),
    )

def closed_form_to_smt2(closed):
    from rec_solver.core.utils import restore_exact_rationals
    solver = z3.Solver()
    closed_dict = closed.as_dict()
    # apps = reduce(set.union, [k.atoms(AppliedUndef) | v.atoms(AppliedUndef) for k, v in closed_dict.items()])
    apps = reduce(set.union, [get_applied_functions(k) for k in closed_dict], set())
    # apps = reduce(set.union, [get_applied_functions(k) | get_applied_functions(v) for k, v in closed_dict.items()])

    # remove the last argument from the function applications
    remove_func_mapping = []
    for f in apps:
        assert(f.decl().arity() > 0)
        if f.decl().arity() == 1:
            remove_func_mapping.append((f, z3.Int(f.decl().name())))
        else:
            args = f.children()[:-1]
            new_func = z3.Function(f.decl().name(), *[arg.sort() for arg in args], z3.IntSort())
            remove_func_mapping.append((f, new_func(args)))

    # remove_func_mapping = [(f, z3.Int(f.decl().name())) for f in apps]
    # new_closed_dict = {k.subs(remove_func_mapping, simultaneous=True): v.subs(remove_func_mapping, simultaneous=True) for k, v in closed_dict.items()}
    new_closed_dict = {z3.substitute(k, remove_func_mapping): z3.substitute(v, remove_func_mapping) for k, v in closed_dict.items()}
    for k, e in new_closed_dict.items():
        # Preserve the legacy representation for consumers without a rational
        # base/step certificate. Certified accelerators consume the separate
        # exact mathematical candidate, never reinterpret a C division.
        solver.add(k == e)
        exact = restore_exact_rationals(e)
        if not z3.eq(exact, e):
            marker = z3.Function("ari_recurrence_exact", z3.RealSort(), z3.RealSort())
            solver.add(marker(k) == exact)
    return solver.to_smt2()

def solve_str_to_smt2(recurrence):
    return closed_form_to_smt2(solve_str(recurrence, **solver_options()))

def solve_coarse_str_to_smt2(recurrence, degree=None):
    """Synthesize observables for the union-star, never a guessed branch word.

    Polynomial-expression synthesis uses *all* transition maps, independently
    of their guards. The caller must certify the resulting base/step relation.
    Keep this request separate from the ordinary solver and its cache.
    """
    from rec_solver.rec_parser import parse_str
    from rec_solver.core.recurrence import LoopRecurrence
    from rec_solver.core.poly_expr import poly_expr_solving
    from rec_solver.core.closed_form import ExprClosedForm

    rec = parse_str(recurrence)
    if not isinstance(rec, LoopRecurrence):
        raise ValueError("coarse summaries require a scalar loop recurrence")
    # The CLI's legacy default is general/order-4, which is unnecessarily
    # expensive for the fallback. Give the bounded coarse search its own
    # knobs so it remains useful under a short verification budget.
    strategy = os.environ.get("ARITHEXE_COARSE_POLY_EXPR_STRATEGY", "auto")
    order = env_int("ARITHEXE_COARSE_POLY_EXPR_ORDER", 1)
    # One synthesis degree per request. The execution engine retries degree 3
    # only when the degree-2 summary cannot discharge the property.
    if degree is None:
        degree = env_optional_int("ARITHEXE_COARSE_POLY_EXPR_DEGREE") or 2
    if degree < 1:
        raise ValueError("coarse polynomial degree must be positive")
    closed = {}
    result = poly_expr_solving(rec, degree, strategy=strategy, order=order)
    for lhs, rhs in result.as_dict().items():
        if not z3.is_true(z3.simplify(lhs == rhs)):
            closed[lhs] = rhs
    return closed_form_to_smt2(ExprClosedForm(closed, rec.ind_var))
