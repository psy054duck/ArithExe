import z3
import sympy as sp
from dataclasses import dataclass
from itertools import combinations_with_replacement, product
from .recurrence import Recurrence, LoopRecurrence
from .closed_form import ExprClosedForm
from . import utils

POLY_EXPR_STRATEGY_AUTO = 'auto'
POLY_EXPR_STRATEGY_SPECIAL = 'special'
POLY_EXPR_STRATEGY_GENERAL = 'general'
POLY_EXPR_STRATEGY_ALGORITHM2 = 'algorithm2'
POLY_EXPR_STRATEGIES = {
    POLY_EXPR_STRATEGY_AUTO,
    POLY_EXPR_STRATEGY_SPECIAL,
    POLY_EXPR_STRATEGY_GENERAL,
    POLY_EXPR_STRATEGY_ALGORITHM2,
}


class GeneralCFiniteError(Exception):
    pass


@dataclass(frozen=True)
class GeneralCFiniteSpace:
    recurrence_coeffs: tuple
    basis_vectors: tuple
    basis_instances: tuple


def normalize_poly_expr_strategy(strategy):
    if strategy is None:
        return POLY_EXPR_STRATEGY_AUTO
    strategy = str(strategy).strip().lower().replace('_', '-')
    aliases = {
        'default': POLY_EXPR_STRATEGY_AUTO,
        'algorithm1': POLY_EXPR_STRATEGY_GENERAL,
        'algorithm-1': POLY_EXPR_STRATEGY_GENERAL,
        'general-algorithm': POLY_EXPR_STRATEGY_GENERAL,
        'algorithm-2': POLY_EXPR_STRATEGY_ALGORITHM2,
        'nested': POLY_EXPR_STRATEGY_ALGORITHM2,
        'nondet': POLY_EXPR_STRATEGY_ALGORITHM2,
        'spectral': POLY_EXPR_STRATEGY_ALGORITHM2,
        'spectral-nd': POLY_EXPR_STRATEGY_ALGORITHM2,
    }
    strategy = aliases.get(strategy, strategy)
    if strategy not in POLY_EXPR_STRATEGIES:
        raise ValueError(
            'Unsupported polynomial-expression strategy "%s". Expected one of %s.'
            % (strategy, ', '.join(sorted(POLY_EXPR_STRATEGIES)))
        )
    return strategy


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
    for tran in transitions:
        next_poly = poly_template.as_expr().subs({ind_var: ind_var + 1}, simultaneous=True)
        poly_prime = next_poly.as_expr().subs(tran, simultaneous=True).as_poly(*X)
        coords = [mono.as_expr().subs({ind_var: ind_var + 1}, simultaneous=True).subs(tran, simultaneous=True).as_poly(*X) for mono in monomials]
        M = sp.Matrix([[coord.coeff_monomial(mono) for mono in monomials] for coord in coords]).T
        # M already has one row for each monomial of degree at most d. The
        # higher-degree equations are enforced later when k is fixed.
        projected_matrix = M
        if possible_k is None:
            possible_k = set(projected_matrix.eigenvals().keys())
        else:
            possible_k = possible_k.intersection(projected_matrix.eigenvals().keys())

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

def find_general_cfinite_spaces(X, transitions, ind_var, d, order):
    """
    General Algorithm 1: find all degree-d polynomial expressions satisfying a
    homogeneous C-finite recurrence of the given order on every branch word.
    """
    if order < 1:
        raise ValueError("The C-finite recurrence order must be positive.")
    if len(transitions) == 0:
        return []

    poly_template, coeffs, _ = gen_poly_template(X, d)
    recurrence_coeffs = list(sp.symbols('__cfinite_c:%d' % order, Real=True))
    equations = _general_cfinite_equations(
        poly_template.as_expr(),
        recurrence_coeffs,
        X,
        transitions,
        ind_var,
        order,
    )
    coefficient_equations = _coefficient_equations(equations, X, ind_var)
    z3_equations = [
        _to_z3_real(sp.expand(eq)) == 0
        for eq in coefficient_equations
        if sp.simplify(eq) != 0
    ]

    spaces = []
    known_basis = []
    for _ in range(len(coeffs) + 1):
        small_space = _find_small_rational_space(
            coefficient_equations,
            recurrence_coeffs,
            coeffs,
            known_basis,
            order,
        )
        if small_space is None:
            outside_span = _outside_span_constraint(coeffs, known_basis)
            if outside_span is None:
                break

            solver = z3.Solver()
            solver.add(z3_equations)
            solver.add(outside_span)
            sat_res = solver.check()
            if sat_res == z3.unsat:
                break
            if sat_res == z3.unknown:
                raise GeneralCFiniteError("Z3 returned unknown while solving Algorithm 1.")

            model = solver.model()
            recurrence_values = [
                _z3_model_value_to_sympy(
                    model.eval(_to_z3_real(c), model_completion=True)
                )
                for c in recurrence_coeffs
            ]
            fixed_equations = _substitute_recurrence_values(
                coefficient_equations,
                recurrence_coeffs,
                recurrence_values,
            )
            basis = _linear_solution_basis(fixed_equations, coeffs)
            new_vectors = _basis_vectors_extending_span(basis, known_basis)
        else:
            recurrence_values, new_vectors = small_space

        if not new_vectors:
            raise GeneralCFiniteError("Algorithm 1 found a model but no new basis vector.")

        known_basis.extend(new_vectors)
        instances = tuple(
            sp.expand(poly_template.as_expr().subs(
                {c: v for v, c in zip(vec, coeffs)},
                simultaneous=True,
            ))
            for vec in new_vectors
        )
        spaces.append(GeneralCFiniteSpace(
            tuple(recurrence_values),
            tuple(new_vectors),
            instances,
        ))

    return spaces


def find_nested_cfinite_spaces(X, transitions, ind_var, d, order):
    """
    Algorithm 2: synthesize common c-finite expressions for nested
    nondeterministic branches using the branch-wise spectral intersection.
    """
    if order < 1:
        raise ValueError("The C-finite recurrence order must be positive.")
    if len(transitions) == 0:
        return []

    root_sets = [
        _candidate_roots_for_transition(transition, X, d, order)
        for transition in transitions
    ]
    common_roots = _intersect_expr_sets(root_sets)
    if len(common_roots) == 0:
        return []

    characteristic_polys = _rational_characteristic_polynomials(common_roots, order)
    poly_template, coeffs, _ = gen_poly_template(X, d)
    spaces = []
    for h_coeffs in characteristic_polys:
        equations = _fixed_characteristic_word_equations(
            poly_template.as_expr(),
            h_coeffs,
            X,
            transitions,
            ind_var,
        )
        coefficient_equations = _coefficient_equations(equations, X, ind_var)
        basis = _linear_solution_basis(coefficient_equations, coeffs)
        if len(basis) == 0:
            continue

        basis_vectors = tuple(sp.Matrix(vec) for vec in basis)
        instances = tuple(
            sp.expand(poly_template.as_expr().subs(
                {c: v for v, c in zip(vec, coeffs)},
                simultaneous=True,
            ))
            for vec in basis_vectors
        )
        spaces.append(GeneralCFiniteSpace(
            _recurrence_coeffs_from_characteristic(h_coeffs),
            basis_vectors,
            instances,
        ))
    return spaces


def find_special_cfinite_spaces(X, transitions, ind_var, d):
    """Return spaces discovered by the original first-order specialized search."""
    spaces = []
    for coeff, basis_instances in vec_space_d(X, transitions, ind_var, d):
        basis_instances = tuple(
            sp.expand(expr.as_expr() if hasattr(expr, "as_expr") else expr)
            for expr in basis_instances
        )
        basis_vectors = tuple(sp.Matrix([sp.Integer(0)]) for _ in basis_instances)
        spaces.append(GeneralCFiniteSpace(
            (sp.simplify(coeff),),
            basis_vectors,
            basis_instances,
        ))
    return spaces


def find_cfinite_spaces(X, transitions, ind_var, d, order, strategy=POLY_EXPR_STRATEGY_AUTO):
    """Return polynomial-expression spaces for the selected synthesis strategy."""
    strategy = normalize_poly_expr_strategy(strategy)
    if strategy == POLY_EXPR_STRATEGY_SPECIAL:
        return find_special_cfinite_spaces(X, transitions, ind_var, d)
    if strategy == POLY_EXPR_STRATEGY_ALGORITHM2:
        return find_nested_cfinite_spaces(X, transitions, ind_var, d, order)
    if strategy == POLY_EXPR_STRATEGY_GENERAL:
        return find_general_cfinite_spaces(X, transitions, ind_var, d, order)

    spaces = find_special_cfinite_spaces(X, transitions, ind_var, d)
    if spaces:
        return spaces
    spaces = find_nested_cfinite_spaces(X, transitions, ind_var, d, order)
    if spaces:
        return spaces
    return find_general_cfinite_spaces(X, transitions, ind_var, d, order)


def _general_cfinite_equations(poly_template, recurrence_coeffs, X, transitions, ind_var, order):
    equations = []
    for branch_word in product(range(len(transitions)), repeat=order):
        iterates = _template_iterates_for_word(poly_template, X, transitions, ind_var, branch_word)
        recurrence_rhs = sp.Integer(0)
        for i, coeff in enumerate(recurrence_coeffs):
            recurrence_rhs += coeff * iterates[order - 1 - i]
        equations.append(sp.expand(iterates[order] - recurrence_rhs))
    return equations


def _fixed_characteristic_word_equations(poly_template, h_coeffs, X, transitions, ind_var):
    order = len(h_coeffs) - 1
    equations = []
    for branch_word in product(range(len(transitions)), repeat=order):
        iterates = _template_iterates_for_word(poly_template, X, transitions, ind_var, branch_word)
        equations.append(sp.expand(sum(
            coeff * iterate
            for coeff, iterate in zip(h_coeffs, iterates)
        )))
    return equations


def _candidate_roots_for_transition(transition, X, d, order):
    linear_part = _linear_part_matrix(transition, X)
    eigenvalues = _unique_exprs(linear_part.eigenvals().keys())
    degree = max(1, get_transition_degr(transition, X))
    spectral_degree = int(d * (degree ** max(order - 1, 0)))

    roots = {sp.Integer(1)}
    if len(eigenvalues) == 0:
        return [sp.Integer(1)]
    for exponents in product(range(spectral_degree + 1), repeat=len(eigenvalues)):
        if sum(exponents) > spectral_degree:
            continue
        root = sp.Integer(1)
        for eigenvalue, exponent in zip(eigenvalues, exponents):
            root *= eigenvalue ** exponent
        roots.add(sp.simplify(root))
    return _unique_exprs(roots)


def _linear_part_matrix(transition, X):
    rows = []
    for var in X:
        rhs = sp.expand(transition.get(var, var))
        poly = sp.Poly(rhs, *X)
        rows.append([sp.simplify(poly.coeff_monomial(x)) for x in X])
    return sp.Matrix(rows)


def _intersect_expr_sets(expr_sets):
    if len(expr_sets) == 0:
        return []
    intersection = list(expr_sets[0])
    for expr_set in expr_sets[1:]:
        intersection = [
            expr
            for expr in intersection
            if any(sp.simplify(expr - candidate) == 0 for candidate in expr_set)
        ]
    return _unique_exprs(intersection)


def _unique_exprs(exprs):
    res = []
    for expr in exprs:
        expr = sp.simplify(expr)
        if any(sp.simplify(expr - existing) == 0 for existing in res):
            continue
        res.append(expr)
    return sorted(res, key=lambda e: str(e))


def _rational_characteristic_polynomials(roots, max_order):
    t = sp.Symbol('__cfinite_t')
    polys = {}
    for degree in range(1, max_order + 1):
        for selected_roots in combinations_with_replacement(roots, degree):
            h = sp.Integer(1)
            for root in selected_roots:
                h *= t - root
            try:
                poly = sp.Poly(sp.expand(h), t)
            except sp.polys.polyerrors.PolynomialError:
                continue
            high_to_low = [sp.simplify(c) for c in poly.all_coeffs()]
            if len(high_to_low) != degree + 1 or sp.simplify(high_to_low[0] - 1) != 0:
                continue
            if not all(_is_rational_expr(c) for c in high_to_low):
                continue
            low_to_high = tuple(sp.Rational(c) for c in reversed(high_to_low))
            polys[low_to_high] = low_to_high
    return sorted(polys.values(), key=lambda coeffs: (len(coeffs), tuple(str(c) for c in coeffs)))


def _is_rational_expr(expr):
    expr = sp.simplify(expr)
    return expr.is_Rational


def _recurrence_coeffs_from_characteristic(h_coeffs):
    order = len(h_coeffs) - 1
    return tuple(sp.simplify(-h_coeffs[order - 1 - i]) for i in range(order))


def _template_iterates_for_word(poly_template, X, transitions, ind_var, branch_word):
    state = list(X)
    iterates = [_eval_template_at_state(poly_template, state, X, ind_var, 0)]
    for step, branch_idx in enumerate(branch_word):
        state = _advance_state_symbolic(state, transitions[branch_idx], X, ind_var, step)
        iterates.append(_eval_template_at_state(poly_template, state, X, ind_var, step + 1))
    return iterates


def _eval_template_at_state(poly_template, state, X, ind_var, step):
    expr = poly_template.subs({ind_var: ind_var + step}, simultaneous=True)
    return sp.expand(expr.subs(dict(zip(X, state)), simultaneous=True))


def _advance_state_symbolic(state, transition, X, ind_var, step):
    state_subs = dict(zip(X, state))
    next_state = []
    for var in X:
        rhs = transition.get(var, var)
        rhs = rhs.subs({ind_var: ind_var + step}, simultaneous=True)
        next_state.append(sp.expand(rhs.subs(state_subs, simultaneous=True)))
    return next_state


def _advance_state_from_initial(state, transition, X, ind_var, step):
    state_subs = dict(zip(X, state))
    next_state = []
    for var in X:
        rhs = transition.get(var, var)
        rhs = rhs.subs({ind_var: sp.Integer(step)}, simultaneous=True)
        next_state.append(sp.expand(rhs.subs(state_subs, simultaneous=True)))
    return next_state


def _coefficient_equations(equations, X, ind_var):
    poly_vars = _dedup_symbols(list(X) + [ind_var])
    coeffs = []
    for eq in equations:
        eq = sp.expand(eq)
        if len(poly_vars) == 0:
            coeffs.append(eq)
            continue
        try:
            poly = sp.Poly(eq, *poly_vars)
        except sp.polys.polyerrors.PolynomialError as exc:
            raise GeneralCFiniteError("Algorithm 1 generated a non-polynomial equation.") from exc
        coeffs.extend(poly.coeffs())
    return coeffs


def _dedup_symbols(symbols):
    res = []
    seen = set()
    for symbol in symbols:
        if symbol in seen:
            continue
        seen.add(symbol)
        res.append(symbol)
    return res


def _linear_solution_basis(equations, coeffs):
    equations = [sp.expand(eq) for eq in equations if sp.simplify(eq) != 0]
    if len(equations) == 0:
        return [sp.eye(len(coeffs)).col(i) for i in range(len(coeffs))]
    matrix, _ = sp.linear_eq_to_matrix(equations, coeffs)
    return matrix.nullspace()


def _find_small_rational_space(coefficient_equations, recurrence_coeffs, coeffs, known_basis, order):
    for recurrence_values in _small_recurrence_value_tuples(order):
        fixed_equations = _substitute_recurrence_values(
            coefficient_equations,
            recurrence_coeffs,
            recurrence_values,
        )
        basis = _linear_solution_basis(fixed_equations, coeffs)
        new_vectors = _basis_vectors_extending_span(basis, known_basis)
        if new_vectors:
            return tuple(recurrence_values), new_vectors
    return None


def _small_recurrence_value_tuples(order):
    nonzero_values = [
        sp.Integer(1),
        sp.Integer(-1),
        sp.Integer(2),
        sp.Integer(-2),
        sp.Integer(3),
        sp.Integer(-3),
        sp.Rational(1, 2),
        sp.Rational(-1, 2),
    ]
    values = [sp.Integer(0)] + nonzero_values
    seen = set()

    def emit(candidate):
        candidate = tuple(candidate)
        if candidate in seen:
            return
        seen.add(candidate)
        return candidate

    for i in range(order):
        for value in nonzero_values:
            candidate = [sp.Integer(0)] * order
            candidate[i] = value
            emitted = emit(candidate)
            if emitted is not None:
                yield emitted

    if order >= 2:
        for left, right in product(values, repeat=2):
            candidate = [sp.Integer(0)] * order
            candidate[0] = left
            candidate[1] = right
            emitted = emit(candidate)
            if emitted is not None:
                yield emitted

    if order <= 2:
        for candidate in product(values, repeat=order):
            emitted = emit(candidate)
            if emitted is not None:
                yield emitted

    zero = emit([sp.Integer(0)] * order)
    if zero is not None:
        yield zero


def _substitute_recurrence_values(equations, recurrence_coeffs, recurrence_values):
    return [
        sp.expand(eq.subs(
            dict(zip(recurrence_coeffs, recurrence_values)),
            simultaneous=True,
        ))
        for eq in equations
    ]


def _basis_vectors_extending_span(basis, known_basis):
    new_vectors = []
    local_basis = list(known_basis)
    for vec in basis:
        vec = sp.Matrix(vec)
        if sp.simplify(vec.dot(vec)) == 0:
            continue
        if _extends_span(local_basis, vec):
            local_basis.append(vec)
            new_vectors.append(vec)
    return new_vectors


def _outside_span_constraint(coeffs, basis_vectors):
    if len(basis_vectors) == 0:
        return z3.Or([_to_z3_real(c) != 0 for c in coeffs])

    basis_matrix = sp.Matrix.hstack(*basis_vectors)
    nullspace = basis_matrix.T.nullspace()
    if len(nullspace) == 0:
        return None

    coeff_vector = sp.Matrix(coeffs)
    clauses = []
    for normal in nullspace:
        normal = sp.Matrix(normal)
        clauses.append(_to_z3_real(sp.expand(normal.dot(coeff_vector))) != 0)
    return z3.Or(clauses)


def _extends_span(basis_vectors, candidate):
    if len(basis_vectors) == 0:
        return True
    old_rank = sp.Matrix.hstack(*basis_vectors).rank()
    new_rank = sp.Matrix.hstack(*(basis_vectors + [candidate])).rank()
    return new_rank > old_rank


def _z3_model_value_to_sympy(value):
    value = z3.simplify(value)
    if z3.is_rational_value(value):
        return sp.Rational(value.numerator_as_long(), value.denominator_as_long())
    if z3.is_int_value(value):
        return sp.Integer(value.as_long())
    if z3.is_algebraic_value(value):
        decimal = value.as_decimal(50).rstrip('?')
        return sp.nsimplify(decimal)
    return sp.sympify(str(value))


def _to_z3_real(expr):
    expr = sp.sympify(expr)
    if isinstance(expr, sp.Add):
        return z3.simplify(sum(_to_z3_real(arg) for arg in expr.args))
    if isinstance(expr, sp.Mul):
        res = z3.RealVal(1)
        for arg in expr.args:
            res *= _to_z3_real(arg)
        return z3.simplify(res)
    if isinstance(expr, sp.Pow):
        base, exponent = expr.as_base_exp()
        if exponent == sp.Rational(1, 2):
            return z3.Sqrt(_to_z3_real(base))
        return z3.simplify(_to_z3_real(base) ** _to_z3_real(exponent))
    if isinstance(expr, sp.Integer) or isinstance(expr, int):
        return z3.RealVal(int(expr))
    if isinstance(expr, sp.Rational):
        return z3.RealVal("%d/%d" % (expr.p, expr.q))
    if isinstance(expr, sp.Symbol):
        return z3.Real(str(expr))
    if expr.is_number:
        return z3.RealVal(str(sp.N(expr, 50)))
    return utils.to_z3(expr, sort='real')


def _closed_forms_from_general(X, transitions, initial, ind_var, degr, order):
    closed_forms = {}
    spaces = find_general_cfinite_spaces(X, transitions, ind_var, degr, order)
    for space in spaces:
        for p in space.basis_instances:
            solved = solve_cfinite_rec(space.recurrence_coeffs, p, transitions, initial, X, ind_var)
            if solved is None:
                continue
            expr, closed = solved
            if isinstance(expr, sp.core.numbers.One):
                continue
            try:
                closed_forms[utils.to_z3(sp.expand(expr))] = utils.to_z3(sp.expand(closed))
            except Exception:
                continue
    return closed_forms


def _closed_forms_from_algorithm2(X, transitions, initial, ind_var, degr, order):
    closed_forms = {}
    spaces = find_nested_cfinite_spaces(X, transitions, ind_var, degr, order)
    for space in spaces:
        for p in space.basis_instances:
            solved = solve_cfinite_rec(space.recurrence_coeffs, p, transitions, initial, X, ind_var)
            if solved is None:
                continue
            expr, closed = solved
            if isinstance(expr, sp.core.numbers.One):
                continue
            try:
                closed_forms[utils.to_z3(sp.expand(expr))] = utils.to_z3(sp.expand(closed))
            except Exception:
                continue
    return closed_forms


def solve_cfinite_rec(recurrence_coeffs, p, transitions, inits, X, ind_var):
    order = len(recurrence_coeffs)
    initial_values = _branch_independent_initial_values(p, transitions, inits, X, ind_var, order)
    if initial_values is None:
        return None
    if order == 1:
        closed = sp.simplify((recurrence_coeffs[0] ** ind_var) * initial_values[0])
        return p, closed

    seq = sp.Function('__cfinite_seq')
    recurrence = sp.Eq(
        seq(ind_var + order),
        sum(
            coeff * seq(ind_var + order - 1 - i)
            for i, coeff in enumerate(recurrence_coeffs)
        ),
    )
    initial_conditions = {
        seq(i): value
        for i, value in enumerate(initial_values)
    }
    try:
        closed = sp.rsolve(recurrence, seq(ind_var), initial_conditions)
    except Exception:
        return None
    if closed is None:
        return None
    return p, sp.simplify(closed)


def _branch_independent_initial_values(p, transitions, inits, X, ind_var, order):
    states = [list(X)]
    initial_values = []
    for step in range(order):
        values = []
        for state in states:
            value = p.subs({ind_var: sp.Integer(step)}, simultaneous=True)
            value = value.subs(dict(zip(X, state)), simultaneous=True)
            value = sp.expand(value.subs(inits, simultaneous=True))
            values.append(value)
        if not all(sp.simplify(value - values[0]) == 0 for value in values):
            return None
        initial_values.append(values[0])
        if step + 1 < order:
            next_states = []
            for state in states:
                for transition in transitions:
                    next_states.append(_advance_state_from_initial(state, transition, X, ind_var, step))
            states = next_states
    return initial_values


def solve_rec(k, p, transitions, inits, ind_var):
    if k != 1:
        if k == 0:
            if sp.simplify(p.subs(inits, simultaneous=True) == 0):
                return (p, 0)
            else:
                return (p, sp.Piecewise((0, ind_var >= 1), (p.subs(inits, simultaneous=True), True)))
        return (p, sp.simplify((k ** ind_var) * p.subs(inits, simultaneous=True)))
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

def _closed_forms_from_special(X, transitions, initial, ind_var, degr):
    ks_instances = vec_space_d(X, transitions, ind_var, degr)
    closed_forms = {}
    for k, instances in ks_instances:
        for p in instances:
            solved = solve_rec(k, p, transitions, initial, ind_var)
            if solved is None:
                continue
            expr, closed = solved
            if isinstance(expr, sp.core.numbers.One): continue
            closed_forms[utils.to_z3(sp.expand(expr))] = utils.to_z3(sp.expand(closed))
    return closed_forms


def poly_expr_solving(
    rec: LoopRecurrence,
    degr=2,
    strategy=POLY_EXPR_STRATEGY_AUTO,
    order=1,
):
    # X = [func.subs({rec.ind_var: rec.ind_var - 1}) for func in rec.all_funcs]
    strategy = normalize_poly_expr_strategy(strategy)
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

    closed_forms = {}
    if strategy in {POLY_EXPR_STRATEGY_AUTO, POLY_EXPR_STRATEGY_SPECIAL}:
        closed_forms |= _closed_forms_from_special(all_vars, transitions, initial, ind_var, degr)

    should_run_algorithm2 = strategy == POLY_EXPR_STRATEGY_ALGORITHM2 or (
        strategy == POLY_EXPR_STRATEGY_AUTO and len(closed_forms) == 0
    )
    if should_run_algorithm2:
        try:
            closed_forms |= _closed_forms_from_algorithm2(all_vars, transitions, initial, ind_var, degr, order)
        except GeneralCFiniteError:
            pass

    should_run_general = strategy == POLY_EXPR_STRATEGY_GENERAL or (
        strategy == POLY_EXPR_STRATEGY_AUTO and len(closed_forms) == 0
    )
    if should_run_general:
        try:
            closed_forms |= _closed_forms_from_general(all_vars, transitions, initial, ind_var, degr, order)
        except GeneralCFiniteError:
            pass

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
