import z3
import os

from ..rec_parser import parse_str
from .recurrence import LoopRecurrence, MultiRecurrence
from .multivariate import solve_multivariate_rec
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from .poly_expr import poly_expr_solving
from .bounded_cfinite import can_attempt_bounded_cfinite_map, solve_bounded_cfinite_map
from .solvable_polynomial import is_solvable_map, solve_solvable_map
from .closed_form import ExprClosedForm


def _closed_form_has_content(closed_form):
    if closed_form is None:
        return False
    try:
        closed_dict = closed_form.as_dict()
    except AttributeError:
        return True
    if not closed_dict:
        return False
    return any(
        not z3.is_true(z3.simplify(lhs == rhs))
        for lhs, rhs in closed_dict.items()
    )


def _direct_function_closed_form_is_inductive(rec, closed_form):
    try:
        closed_dict = closed_form.as_dict()
    except AttributeError:
        return True

    ind_var = rec.ind_var
    func_closed = {}
    for func_decl in rec.func_decls:
        if func_decl.arity() != 1:
            continue
        app = func_decl(ind_var)
        if app in closed_dict:
            func_closed[app] = closed_dict[app]

    if not func_closed:
        return True

    solver = z3.Solver()
    # UNKNOWN/timeouts reject the candidate; never accept an unchecked map.
    try:
        timeout = int(os.environ.get('ARITHEXE_RECURRENCE_CERT_TIMEOUT_MS', '3000'))
    except ValueError:
        timeout = 3000
    solver.set(timeout=max(1, timeout))
    for app, expr in func_closed.items():
        initial_app = app.decl()(z3.IntVal(0))
        if initial_app not in rec.initial:
            continue
        initial_expr = rec.initial[initial_app]
        initial_value = z3.simplify(
            z3.substitute(expr, (ind_var, z3.IntVal(0)))
        )
        solver.push()
        solver.add(initial_value != initial_expr)
        if solver.check() != z3.unsat:
            solver.pop()
            return False
        solver.pop()

    current_mapping = list(func_closed.items())
    for condition, transition in zip(rec.conditions, rec.transitions):
        closed_condition = z3.simplify(
            z3.substitute(condition, *current_mapping)
        )
        for lhs_next, rhs in transition.items():
            if lhs_next.decl().arity() != 1:
                continue
            app = lhs_next.decl()(ind_var)
            if app not in func_closed:
                continue
            expected_next = z3.simplify(
                z3.substitute(func_closed[app], (ind_var, ind_var + 1))
            )
            actual_next = z3.simplify(z3.substitute(rhs, *current_mapping))
            solver.push()
            solver.add(ind_var >= 0)
            solver.add(closed_condition)
            solver.add(expected_next != actual_next)
            if solver.check() != z3.unsat:
                solver.pop()
                return False
            solver.pop()

    return True


def solve_str(
    s,
    enable_bounded_cfinite=True,
    poly_expr_strategy="auto",
    poly_expr_order=1,
    poly_expr_degree=None,
    periodic_strategy="compression",
):
    rec = parse_str(s)
    if isinstance(rec, LoopRecurrence):
        if periodic_strategy == "oopsla23":
            # Isolate the published periodic route: no direct/coarse fallback.
            result = solve_ultimately_periodic_symbolic(
                rec, enable_bounded_cfinite=False, pattern_strategy="oopsla23")
            if not _direct_function_closed_form_is_inductive(rec, result):
                raise ValueError('OOPSLA23 solution failed initial/transition certification')
            return result
        if periodic_strategy != "compression":
            raise ValueError('Unknown periodic strategy: ' + periodic_strategy)
        solvable = False
        try:
            solvable = is_solvable_map(rec)
        except:
            pass
        if solvable and len(rec.transitions) == 1:
            # No branch-pattern sampling/QE is needed for an unconditional
            # solvable map. Work on a copy: layer solving substitutes in place.
            try:
                direct_rec = rec.copy_rec_with_diff_initial(dict(rec.initial))
                result = ExprClosedForm(solve_solvable_map(direct_rec), rec.ind_var)
                if _closed_form_has_content(result):
                    return result
            except Exception:
                # Preserve the existing general fallback on unsupported maps.
                pass
        if enable_bounded_cfinite and not solvable and can_attempt_bounded_cfinite_map(rec, require_nonlinear=True):
            try:
                res = solve_bounded_cfinite_map(rec)
                if (_closed_form_has_content(res) and
                        _direct_function_closed_form_is_inductive(rec, res)):
                    return res
            except:
                pass
        try:
            res = solve_ultimately_periodic_symbolic(
                rec,
                enable_bounded_cfinite=enable_bounded_cfinite,
            )
            if _closed_form_has_content(res):
                return res
        except:
            pass
        if enable_bounded_cfinite:
            try:
                res = solve_bounded_cfinite_map(rec)
                if (_closed_form_has_content(res) and
                        _direct_function_closed_form_is_inductive(rec, res)):
                    return res
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
            if _closed_form_has_content(res):
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
