"""Guarded numerical instantiation of definition/unfolding/folding tupling.

The tuple denotes (f(T_0(x)), ..., f(T_m(x))), with T_0 the identity.
This is a bounded specialization strategy, not a reimplementation of ECCE or
the full structural MRP analysis. See docs/tupling.md for scope and references.
"""

from dataclasses import dataclass

import sympy as sp
import z3

from . import utils
from .closed_form import MultiFuncClosedForm
from .recurrence import BaseCase, MultiRecurrence, RecursiveCase


class TuplingUnsupported(ValueError):
    """No certified finite, solver-compatible tuple was found within the limits."""


@dataclass(frozen=True)
class TuplingOptions:
    max_components: int = 8
    max_rounds: int = 12
    max_branches: int = 128
    solver_timeout_ms: int = 1000

    def __post_init__(self):
        if min(self.max_components, self.max_rounds, self.max_branches,
               self.solver_timeout_ms) <= 0:
            raise ValueError("tupling limits must be positive")


@dataclass
class TuplingResult:
    recurrence: MultiRecurrence
    components: tuple
    ranking: z3.ArithRef
    rounds: int
    proof_obligations: int


class _Tupler:
    def __init__(self, rec, options):
        self.rec = rec
        self.options = options
        self.args = tuple(rec.func_sig.children())
        self.func = rec.func_sig.decl()
        self.cases = rec.get_base_cases() + rec.get_rec_cases()
        self.obligations = 0
        self.query_cache = {}
        self.solver = z3.Solver()
        self.solver.set(timeout=options.solver_timeout_ms)
        self.inverse_cache = {}
        if not rec.get_base_cases():
            raise TuplingUnsupported("source recurrence has no base cases")
        if rec.number_ret() != 1 or not self.args:
            raise TuplingUnsupported("tupling requires a scalar-valued recurrence")
        if (any(not z3.is_const(a) or a.sort() != z3.IntSort() for a in self.args)
                or len(set(self.args)) != len(self.args)):
            raise TuplingUnsupported("formal arguments must be distinct integer variables")
        for case in self.cases:
            if len(case.op) != 1 or not isinstance(case.op[0], z3.ExprRef):
                raise TuplingUnsupported("unsupported source result representation")
            if self.calls(case.condition):
                raise TuplingUnsupported("recursive calls in guards are unsupported")
            if isinstance(case, BaseCase) and self.calls(case.op[0]):
                raise TuplingUnsupported("recursive calls in source base case")
            if isinstance(case, RecursiveCase):
                self.check_linear(case.op[0], tuple(case.recursive_calls))
                for call in case.recursive_calls.values():
                    if call.decl() != self.func or self.calls_in_args(call):
                        raise TuplingUnsupported("nested or mutually recursive calls are unsupported")
                    self.affine(call.children())
        coverage = z3.Or(*[c.condition for c in self.cases])
        self.require(z3.BoolVal(True), coverage, "source cases do not cover the integer domain")
        for i, left in enumerate(self.cases):
            for right in self.cases[i + 1:]:
                if self.feasible(z3.And(left.condition, right.condition)):
                    raise TuplingUnsupported("source cases overlap")

    def feasible(self, condition):
        condition = z3.simplify(condition)
        key = condition.sexpr()
        if key not in self.query_cache:
            self.solver.push()
            self.solver.add(condition)
            answer = self.solver.check()
            self.solver.pop()
            self.obligations += 1
            if answer == z3.unknown:
                raise TuplingUnsupported("SMT solver returned unknown during tupling")
            self.query_cache[key] = answer == z3.sat
        return self.query_cache[key]

    def proves(self, condition, conclusion):
        return not self.feasible(z3.And(condition, z3.Not(conclusion)))

    def require(self, condition, conclusion, message):
        if not self.proves(condition, conclusion):
            raise TuplingUnsupported(message)

    def at(self, expr, values):
        return z3.simplify(z3.substitute(expr, list(zip(self.args, values))))

    def calls(self, expression):
        found = set()
        todo = [expression]
        while todo:
            expr = todo.pop()
            if z3.is_app(expr) and expr.decl() == self.func:
                found.add(expr)
            todo.extend(expr.children())
        return sorted(found, key=lambda call: call.sexpr())

    def calls_in_args(self, call):
        return any(self.calls(arg) for arg in call.children())

    def rhs(self, case, values):
        expr = case.op[0]
        if isinstance(case, RecursiveCase):
            expr = z3.substitute(expr, list(case.recursive_calls.items()))
        return self.at(expr, values)

    def affine(self, values):
        symbols = [utils.to_sympy(a) for a in self.args]
        rows, constants = [], []
        try:
            for value in values:
                poly = sp.Poly(utils.to_sympy(value), *symbols)
                if poly.total_degree() > 1:
                    raise ValueError("non-affine argument")
                row = [poly.coeff_monomial(s) for s in symbols]
                constant = poly.coeff_monomial(1)
                if any(not c.is_Integer for c in row + [constant]):
                    raise ValueError("nonconstant integer coefficient")
                rows.append(row)
                constants.append(constant)
        except (ValueError, TypeError, sp.PolynomialError, NotImplementedError) as error:
            raise TuplingUnsupported("arguments must be integer affine maps") from error
        return sp.Matrix(rows), sp.Matrix(constants)

    def inverse(self, values):
        key = tuple(v.sexpr() for v in values)
        if key not in self.inverse_cache:
            matrix, offset = self.affine(values)
            if abs(matrix.det()) != 1:
                raise TuplingUnsupported("fold anchor has no integer affine inverse")
            inv = matrix.inv()
            shifted = [a - int(c) for a, c in zip(self.args, offset)]
            self.inverse_cache[key] = tuple(z3.simplify(sum(
                int(inv[i, j]) * shifted[j] for j in range(len(self.args))))
                for i in range(len(self.args)))
        return self.inverse_cache[key]

    @staticmethod
    def check_linear(expr, results):
        if not results:
            return
        zero = [(v, z3.IntVal(0)) for v in results]
        remainder = z3.simplify(z3.substitute(expr, zero))
        rebuilt = remainder
        for result in results:
            unit = [(v, z3.IntVal(int(v.eq(result)))) for v in results]
            coefficient = z3.simplify(z3.substitute(expr, unit) - remainder)
            if not z3.is_int_value(coefficient):
                raise TuplingUnsupported("post-operation must have constant integer result coefficients")
            rebuilt = rebuilt + coefficient * result
        if not z3.is_true(z3.simplify(expr == rebuilt)):
            solver = z3.Solver()
            solver.set(timeout=1000)
            solver.add(expr != rebuilt)
            if solver.check() != z3.unsat:
                raise TuplingUnsupported("nonlinear recursive-result combination")

    def unfold(self, components):
        rows = [(z3.BoolVal(True), ())]
        for index, component in enumerate(components):
            new_rows = []
            for guard, expressions in rows:
                for case in self.cases:
                    condition = z3.simplify(z3.And(guard, self.at(case.condition, component)))
                    if self.feasible(condition):
                        # Unfold the selected computation, not every tuple member:
                        # e.g. retain f(n-1) so it folds to the child's first result.
                        expr = (self.rhs(case, component)
                                if index == 0 or isinstance(case, BaseCase)
                                else self.func(*component))
                        new_rows.append((condition, expressions + (expr,)))
                        if len(new_rows) > self.options.max_branches:
                            raise TuplingUnsupported("tupling branch limit exceeded")
            rows = new_rows
        return rows

    def reduce_base_calls(self, guard, expressions):
        mapping = []
        for call in self.all_calls(expressions):
            for case in self.rec.get_base_cases():
                if self.proves(guard, self.at(case.condition, call.children())):
                    mapping.append((call, self.rhs(case, call.children())))
                    break
        return tuple(z3.simplify(z3.substitute(e, mapping)) for e in expressions)

    def all_calls(self, expressions):
        return sorted({c for e in expressions for c in self.calls(e)},
                      key=lambda c: c.sexpr())

    def equivalent(self, guard, left, right):
        return self.proves(guard, z3.And(*[a == b for a, b in zip(left, right)]))

    def fold(self, guard, expressions, components):
        calls = self.all_calls(expressions)
        if not calls:
            return None, (), expressions
        candidates = []
        for call in calls:
            anchor = tuple(call.children())
            try:
                inverse = self.inverse(anchor)
            except TuplingUnsupported:
                continue
            transformed = [tuple(self.at(v, anchor) for v in t) for t in components]
            missing, matches = [], []
            for leaf in calls:
                match = next((i for i, values in enumerate(transformed)
                              if self.equivalent(guard, leaf.children(), values)), None)
                if match is None:
                    relative = tuple(self.at(v, inverse) for v in leaf.children())
                    missing.append(relative)
                else:
                    matches.append((leaf, match))
            # Prefer existing folds, then small argument shifts (e.g. n-1 over n-2).
            matrix, offset = self.affine(anchor)
            cost = sum(abs(int(c)) for c in matrix - sp.eye(len(self.args)))
            cost += sum(abs(int(c)) for c in offset)
            candidates.append((len(missing), cost, call.sexpr(), anchor, missing, matches))
        if not candidates:
            raise TuplingUnsupported("no invertible affine fold anchor")
        _, _, _, anchor, missing, matches = min(candidates, key=lambda c: c[:3])
        return anchor, missing, matches

    def ranking(self, rows):
        candidates = list(self.args) + [-a for a in self.args]
        candidates += [a - b for a in self.args for b in self.args if not a.eq(b)]
        for rank in candidates:
            if all(self.proves(guard, z3.And(rank >= 0, self.at(rank, anchor) < rank))
                   for guard, anchor in rows):
                return rank
        if not rows:
            return z3.IntVal(0)
        raise TuplingUnsupported("no common nonnegative decreasing affine ranking found")

    def run(self):
        components = [self.args]
        for round_number in range(1, self.options.max_rounds + 1):
            rows, additions = [], []
            for guard, expressions in self.unfold(components):
                expressions = self.reduce_base_calls(guard, expressions)
                anchor, missing, matches = self.fold(guard, expressions, components)
                for relative in missing:
                    if not any(self.equivalent(z3.BoolVal(True), relative, t)
                               for t in components + additions):
                        additions.append(relative)
                rows.append((guard, expressions, anchor, matches))
            if additions:
                if len(components) + len(additions) > self.options.max_components:
                    raise TuplingUnsupported("finite tuple component limit exceeded")
                components.extend(additions)
                continue
            rank = self.ranking([(g, a) for g, _, a, _ in rows if a is not None])
            args_sorts = [a.sort() for a in self.args]
            decl = z3.FreshFunction(*args_sorts, z3.IntSort())
            names = tuple(z3.FreshInt("tuple_result") for _ in components)
            base_cases, recursive_cases = [], []
            for guard, expressions, anchor, matches in rows:
                if anchor is None:
                    base_cases.append(BaseCase(guard, expressions))
                    continue
                replacements = [(call, names[i]) for call, i in matches]
                ops = tuple(z3.simplify(z3.substitute(e, replacements)) for e in expressions)
                self.check_linear_vector(ops, names)
                interpretations = [(names[i], self.func(*[
                    self.at(v, anchor) for v in component]))
                    for i, component in enumerate(components)]
                for before, after in zip(expressions, ops):
                    self.require(guard, before == z3.substitute(after, interpretations),
                                 "fold does not preserve a tuple component")
                call = decl(*anchor)
                recursive_cases.append(RecursiveCase(guard, {n: call for n in names}, ops))
            self.require(z3.BoolVal(True), z3.Or(*[g for g, _, _, _ in rows]),
                         "tuple branches do not cover all inputs")
            if not base_cases:
                raise TuplingUnsupported("tuple has no terminating base case")
            result = MultiRecurrence(decl(*self.args), base_cases, recursive_cases)
            if not result.is_nearly_tail():
                raise TuplingUnsupported("residual tuple is not nearly tail-recursive")
            return TuplingResult(result, tuple(components), rank, round_number, self.obligations)
        raise TuplingUnsupported("tupling round limit exceeded")

    def check_linear_vector(self, expressions, names):
        for expr in expressions:
            if self.calls(expr):
                raise TuplingUnsupported("unfolded recursive call escaped folding")
            self.check_linear(expr, names)


def tuple_recurrence(rec, options=None):
    """Build an exact finite tuple, or raise TuplingUnsupported; never use pivot."""
    if not isinstance(rec, MultiRecurrence):
        raise TuplingUnsupported("input must be a MultiRecurrence")
    return _Tupler(rec, options or TuplingOptions()).run()


def solve_tupled_rec(rec, is_array=False, options=None):
    """Solve nearly-tail inputs directly; tuple only when calls need merging."""
    from .dag_reconstruction import solve_nearly_tail

    options = options or TuplingOptions()
    if isinstance(rec, MultiRecurrence) and rec.is_nearly_tail():
        # No fold anchor or inverse is needed, but keep the source checks
        # and termination argument required by closed-form certification.
        checker = _Tupler(rec, options)
        target = MultiRecurrence(
            rec.func_sig,
            [case for case in rec.get_base_cases()
             if checker.feasible(case.condition)],
            [case for case in rec.get_rec_cases()
             if checker.feasible(case.condition)],
        )
        checker.ranking([
            (case.condition, tuple(next(iter(case.recursive_calls.values())).children()))
            for case in target.get_rec_cases()
        ])
    else:
        target = tuple_recurrence(rec, options).recurrence
    from .base_inputs import normalize_base_inputs
    target, restore_inputs = normalize_base_inputs(target, options)
    if not target.get_rec_cases():
        value = z3.IntVal(0)
        for case in reversed(target.get_base_cases()):
            value = z3.If(case.condition, case.op[0], value)
        value = z3.simplify(value)
    else:
        closed = solve_nearly_tail(target, is_array=is_array)
        value = closed[0]
    value = z3.simplify(z3.substitute(value, restore_inputs))
    certify_closed_form(rec, value, options)
    return MultiFuncClosedForm(rec.func_sig, value)


def certify_closed_form(rec, value, options=None):
    """Treat downstream solver output as a candidate, not an exactness proof."""
    checker = _Tupler(rec, options or TuplingOptions())
    allowed_functions = {f.decl() for case in checker.cases
                         for expression in (case.condition, checker.rhs(case, checker.args))
                         for f in utils.get_applied_functions(expression)
                         if f.decl() != checker.func}
    if any(f.decl() not in allowed_functions for f in utils.get_applied_functions(value)):
        raise TuplingUnsupported("scalar closed form contains unresolved function calls")
    for case in checker.cases:
        rhs = checker.rhs(case, checker.args)
        mapping = [(call, checker.at(value, call.children())) for call in checker.calls(rhs)]
        rhs = z3.substitute(rhs, mapping)
        checker.require(case.condition, value == rhs,
                        "scalar closed-form candidate failed an original recurrence equation")
