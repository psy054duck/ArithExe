import math
from itertools import product

import sympy as sp
import z3

from . import utils
from .closed_form import ExprClosedForm
from .recurrence import LoopRecurrence


class BoundedCFiniteError(Exception):
    pass


def solve_bounded_cfinite_map(rec: LoopRecurrence, max_degree_bound=None):
    """
    Implement Algorithm 2 and Algorithm 3 for deterministic polynomial maps.

    Algorithm 2 is used to find/confirm a bounded-degree space. Algorithm 3 then
    builds the composition matrix on that finite polynomial space and computes
    closed forms for the coordinate functions.
    """
    ctx = _extract_polynomial_map(rec)
    degree = max(1, _map_degree(ctx.transitions, ctx.variables))
    upper = max_degree_bound or degree ** max(len(ctx.variables) - 1, 0)
    upper = max(degree, upper)

    for bound in range(degree, upper + 1):
        if is_bounded_degree_map(ctx.transitions, ctx.variables, bound):
            return _closed_form_from_bounded_map(rec, ctx, bound)

    raise BoundedCFiniteError("No bounded C-finite degree bound was found.")


def is_bounded_cfinite_map(rec: LoopRecurrence, max_degree_bound=None):
    try:
        solve_bounded_cfinite_map(rec, max_degree_bound)
        return True
    except BoundedCFiniteError:
        return False


def can_attempt_bounded_cfinite_map(rec, require_nonlinear=False):
    try:
        _ensure_nonconditional_polynomial_map(rec)
        ctx = _extract_polynomial_map(rec)
        if require_nonlinear and _map_degree(ctx.transitions, ctx.variables) <= 1:
            return False
        return True
    except BoundedCFiniteError:
        return False


def is_bounded_degree_map(transitions, variables, degree_bound):
    """
    Algorithm 2: Checking Bounded Degree by the Finite Cutoff.
    """
    cutoff = len(variables) * math.comb(len(variables) + degree_bound, len(variables)) + 1
    q = list(transitions)
    for _ in range(1, cutoff + 1):
        if _map_degree(q, variables) > degree_bound:
            return False
        q = _compose_map(q, transitions, variables)
    return True


def _closed_form_from_bounded_map(rec, ctx, degree_bound):
    """
    Algorithm 3: Computing Closed-form Solutions to C-finite Polynomial Maps.
    """
    basis_exponents = _monomial_exponents(len(ctx.variables), degree_bound)
    basis = [_monomial(ctx.variables, exponents) for exponents in basis_exponents]
    columns = []
    for monomial in basis:
        image = sp.expand(monomial.subs(dict(zip(ctx.variables, ctx.transitions)), simultaneous=True))
        image = _truncate(image, ctx.variables, degree_bound)
        columns.append(_coordinate_vector(image, ctx.variables, basis_exponents))

    matrix = sp.Matrix.hstack(*columns)
    ind_var = utils.to_sympy(rec.ind_var)
    closed_forms = {}
    for func_decl, variable in zip(ctx.func_decls, ctx.variables):
        basis_vector = _coordinate_vector(variable, ctx.variables, basis_exponents)
        powered = (matrix ** ind_var) * basis_vector
        closed_poly = _polynomial_from_vector(powered, basis)
        closed_poly = sp.expand(closed_poly.subs(ctx.initial_subs, simultaneous=True))
        key = func_decl(rec.ind_var)
        closed_forms[key] = z3.simplify(utils.to_z3(closed_poly))
    return ExprClosedForm(closed_forms, rec.ind_var)


class _MapContext:
    def __init__(self, func_decls, variables, transitions, initial_subs):
        self.func_decls = func_decls
        self.variables = variables
        self.transitions = transitions
        self.initial_subs = initial_subs


def _extract_polynomial_map(rec: LoopRecurrence):
    _ensure_nonconditional_polynomial_map(rec)
    func_decls = sorted(rec.func_decls, key=lambda f: f.name())
    current_apps = [func(rec.ind_var) for func in func_decls]
    next_apps = [z3.simplify(func(rec.ind_var + 1)) for func in func_decls]
    tmp_vars_z3 = [z3.Int(f"{func.name()}_tmp") for func in func_decls]
    z3_to_tmp = list(zip(current_apps, tmp_vars_z3))

    transition = {z3.simplify(k): v for k, v in rec.transitions[0].items()}
    sp_variables = [utils.to_sympy(v) for v in tmp_vars_z3]
    sp_transitions = []
    for next_app in next_apps:
        if next_app not in transition:
            raise BoundedCFiniteError(f"Missing transition for {next_app}.")
        rhs = z3.substitute(transition[next_app], z3_to_tmp)
        try:
            sp_transitions.append(sp.expand(utils.to_sympy(rhs)))
        except Exception as exc:
            raise BoundedCFiniteError(f"Transition is not a supported polynomial: {rhs}") from exc

    initial_subs = {}
    for func_decl, variable in zip(func_decls, sp_variables):
        initial_app = func_decl(z3.IntVal(0))
        initial_value = rec.initial.get(initial_app, initial_app)
        initial_subs[variable] = utils.to_sympy(z3.substitute(initial_value, z3_to_tmp))

    return _MapContext(func_decls, sp_variables, sp_transitions, initial_subs)


def _ensure_nonconditional_polynomial_map(rec):
    solver = z3.Solver()
    if len(rec.transitions) != 1 or solver.check(z3.Not(rec.conditions[0])) != z3.unsat:
        raise BoundedCFiniteError("Bounded C-finite solving currently handles one unconditional transition.")
    if any(func.arity() != 1 for func in rec.func_decls):
        raise BoundedCFiniteError("Bounded C-finite solving currently handles scalar loop variables only.")
    if not rec.is_standard():
        raise BoundedCFiniteError("Recurrence is not in standard loop form.")


def _compose_map(left, right, variables):
    return [sp.expand(expr.subs(dict(zip(variables, right)), simultaneous=True)) for expr in left]


def _map_degree(polys, variables):
    degrees = []
    for poly in polys:
        poly = sp.expand(poly)
        if poly == 0:
            degrees.append(0)
        else:
            degrees.append(poly.as_poly(*variables).total_degree())
    return max(degrees, default=0)


def _monomial_exponents(num_variables, degree_bound):
    exponents = [
        exps
        for exps in product(range(degree_bound + 1), repeat=num_variables)
        if sum(exps) <= degree_bound
    ]
    return sorted(exponents, key=lambda exps: (-sum(exps), exps))


def _monomial(variables, exponents):
    res = sp.Integer(1)
    for variable, exponent in zip(variables, exponents):
        res *= variable ** exponent
    return res


def _truncate(poly, variables, degree_bound):
    res = sp.Integer(0)
    as_poly = sp.Poly(sp.expand(poly), *variables)
    for exponents, coeff in as_poly.terms():
        if sum(exponents) <= degree_bound:
            res += coeff * _monomial(variables, exponents)
    return sp.expand(res)


def _coordinate_vector(poly, variables, basis_exponents):
    as_poly = sp.Poly(sp.expand(poly), *variables)
    return sp.Matrix([as_poly.coeff_monomial(exponents) for exponents in basis_exponents])


def _polynomial_from_vector(vector, basis):
    return sp.expand(sum(coeff * monomial for coeff, monomial in zip(vector, basis)))
