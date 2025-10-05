import z3
import sympy as sp
from itertools import product
from .recurrence import Recurrence, LoopRecurrence
from .closed_form import ExprClosedForm
from . import utils

def gen_poly_template(X, d):
    monomials = {sp.Integer(1)}
    for _ in range(d):
        monomials_pair = set(product(X, monomials))
        monomials = monomials.union(set(x*y for x, y in monomials_pair))
    monomials = list(monomials)
    coeffs = sp.symbols('a:%d' % len(monomials), Real=True)
    res = sum([a*m for a, m in zip(coeffs, monomials)])
    return res.as_poly(*X), list(coeffs), monomials

def get_transition_degr(transition, X):
    return max([tran.as_poly(*X).total_degree() for tran in transition.values()])

def get_max_transitions_degr(transitions, X):
    return max([get_transition_degr(tran, X) for tran in transitions])

def vec_space_d(X, transitions, ind_var, d):
    poly_template, coeffs, monomials = gen_poly_template(X, d)
    possible_k = None
    d_p = get_max_transitions_degr(transitions, X)
    _, _, complete_monomials = gen_poly_template(X, d*d_p)
    num_higher_monomials = len(complete_monomials) - len(monomials)
    for tran in transitions:
        next_poly = poly_template.as_expr().subs({ind_var: ind_var + 1}, simultaneous=True)
        poly_prime = next_poly.as_expr().subs(tran, simultaneous=True).as_poly(*X)
        coords = [mono.as_expr().subs({ind_var: ind_var + 1}, simultaneous=True).subs(tran, simultaneous=True).as_poly(*X) for mono in monomials]
        M = sp.Matrix([[coord.coeff_monomial(mono) for mono in monomials] for coord in coords]).T
        # print(M.eigenvects())
        M_lower = M[num_higher_monomials:, :]
        if possible_k is None:
            possible_k = set(M_lower.eigenvals().keys())
        else:
            possible_k = possible_k.intersection(M_lower.eigenvals().keys())

    ret = []
    for k in possible_k - {sp.Integer(1)}:
        all_coeffs = []
        for tran in transitions:
            next_poly = poly_template.as_expr().subs({ind_var: ind_var + 1}, simultaneous=True)
            poly_prime = next_poly.as_expr().subs(tran, simultaneous=True).as_poly(*X)
            # poly_coeffs = poly_prime.coeffs()
            rem = poly_prime - k*poly_template
            rem_coeffs = rem.coeffs()
            all_coeffs.extend(rem_coeffs)
        res, _ = sp.linear_eq_to_matrix(all_coeffs, *coeffs)
        basis = res.nullspace()
        basis_instances = []
        for vec in basis:
            instance = poly_template.subs({c: v for v, c in zip(vec, coeffs)}, simultaneous=True)
            basis_instances.append(instance)
        # symbolic_baiss = [(vec.T * Matrix(coeffs))[0] for vec in basis]
        ret.append((k, basis_instances))
        all_coeffs = []
    all_coeffs = []
    const_dummy_symbol = sp.Symbol('aaaaa0', real=True)
    coeffs.append(const_dummy_symbol)
    for tran in transitions:
        next_poly = poly_template.as_expr().subs({ind_var: ind_var + 1}, simultaneous=True)
        poly_prime = next_poly.as_expr().subs(tran, simultaneous=True).as_poly(*X)
        rem = poly_prime - poly_template - const_dummy_symbol
        rem_coeffs = rem.coeffs()
        all_coeffs.extend(rem_coeffs)
    res, _ = sp.linear_eq_to_matrix(all_coeffs, *coeffs)
    basis = res.nullspace()
    basis_instances = []
    for vec in basis:
        instance = poly_template.subs({c: v for v, c in zip(vec, coeffs)}, simultaneous=True)
        numerator, _ = sp.fraction(sp.factor(instance))
        basis_instances.append(numerator)
    if len(basis) != 0:
        ret.append((sp.Integer(1), basis_instances))
    return ret

def solve_rec(k, p, transitions, inits, ind_var):
    if k != 1:
        if k == 0:
            if sp.simplify(p.subs(inits, simultaneous=True) == 0):
                return (p, 0)
            else:
                return (p, sp.Piecewise((0, ind_var >= 1), (p.subs(inits, simultaneous=True), True)))
    else:
        cs = [sp.simplify(p.subs({ind_var: ind_var + 1}, simultaneous=True).subs(tran, simultaneous=True) - p) for tran in transitions]
        if all([c == cs[0] for c in cs]):
            c = cs[0]
            return (p, sp.simplify(p.subs({ind_var: sp.Integer(0)}, simultaneous=True).subs(inits, simultaneous=True)) + ind_var*c)

def _gen_tmp_var(func):
    '''This is used to replace recursive functions with a temporary variable used in this module'''
    return z3.Int(get_tmp_name(func))

def get_tmp_name(func):
    '''This is used to get the name of the temporary variable used in this module'''
    return func.decl().name() + '_tmp'

def _internalize_transition(transition, rec: LoopRecurrence):
    '''This is used to internalize the transition of a recurrence relation'''
    all_funcs = rec.all_funcs
    internalize_update = lambda up: z3.substitute(up, [(f, _gen_tmp_var(f)) for f in all_funcs])
    z3_res = {_gen_tmp_var(func): internalize_update(update) for func, update in transition.items()}
    sp_res = {utils.to_sympy(func): utils.to_sympy(update) for func, update in z3_res.items()}
    return sp_res

def poly_expr_solving(rec: LoopRecurrence, degr=2):
    # X = [func.subs({rec.ind_var: rec.ind_var - 1}) for func in rec.all_funcs]
    mapping = {func: _gen_tmp_var(func) for func in rec.all_funcs}
    all_vars = [utils.to_sympy(var) for var in list(mapping.values())]
    transitions = [_internalize_transition(tran, rec) for tran in rec.transitions]
    initial = _internalize_transition(rec.initial, rec)
    ind_var = utils.to_sympy(rec.ind_var)
    simple_solutions = {}
    for var in all_vars:
        all_rhs = [sp.simplify(tran[var]) for tran in transitions]
        if _is_simple_rec(var, all_rhs):
            const = sp.simplify(all_rhs[0] - var)
            simple_solutions[utils.to_z3(var)] = utils.to_z3(initial[var] + const*ind_var)

    ks_instances = vec_space_d(all_vars, transitions, ind_var, degr)
    closed_forms = {}
    for k, instances in ks_instances:
        for p in instances:
            expr, closed = solve_rec(k, p, transitions, initial, ind_var)
            if isinstance(expr, sp.core.numbers.One): continue
            closed_forms[utils.to_z3(sp.expand(expr))] = utils.to_z3(sp.expand(closed))
    closed_forms = closed_forms | simple_solutions

    reversed_mapping = {v: k for k, v in mapping.items()}
    ori_closed_forms = {z3.substitute(f, list(reversed_mapping.items())): z3.substitute(closed, list(reversed_mapping.items())) for f, closed in closed_forms.items()}
    return ExprClosedForm(ori_closed_forms, rec.ind_var)

def _is_simple_rec(lhs, all_rhs):
    '''Check if the recurrence is simple, i.e., it has a single variable and a single transition'''
    remainders = []
    for rhs in all_rhs:
        remainder = sp.simplify(rhs - lhs)
        remainders.append(remainder)
    if not all([sp.simplify(r - remainders[0]) == 0 for r in remainders]):
        return False
    
    if remainders[0].is_number:
        return True
    return False
