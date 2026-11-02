"""Exact finite aggregates for triangular affine scalar recurrences.

The proof kernel uses the finite variation-of-constants identity, without
division by a coefficient. SMT certifies the affine decomposition and guards;
only candidates reconstructed by that identity are accepted. SMT consumers
may over-approximate residual aggregate applications, never counterexamples.
"""

from dataclasses import dataclass
import hashlib

import z3
from z3.z3util import get_vars

from .closed_form import ExprClosedForm
from .recurrence import LoopRecurrence


class AggregateUnsupported(ValueError):
    pass


def _walk(expression):
    pending, seen = [expression], set()
    while pending:
        term = pending.pop()
        if term.get_id() in seen:
            continue
        seen.add(term.get_id())
        yield term
        pending.extend(term.children())


def _prove(condition, conclusion):
    solver = z3.Solver(ctx=condition.ctx)
    solver.set(timeout=200)
    solver.add(condition, z3.Not(conclusion))
    return solver.check() == z3.unsat


@dataclass(frozen=True)
class AggregateDefinition:
    kind: str
    function: z3.FuncDeclRef
    index: z3.ExprRef
    parameters: tuple
    body: z3.ExprRef


class AggregateContext:
    """Bind the index; explicitly pass every other free integer variable."""

    def __init__(self):
        self.definitions = {}

    def make(self, kind, index, lower, upper, body):
        if kind not in {"sum", "product"}:
            raise AggregateUnsupported("unknown aggregate kind")
        ctx = index.ctx
        lower, upper = [z3.IntVal(v, ctx) if isinstance(v, int) else v
                        for v in (lower, upper)]
        body = z3.simplify(body)
        if not all(v.sort() == z3.IntSort(ctx) for v in (index, lower, upper, body)):
            raise AggregateUnsupported("aggregate terms must be integers")
        count = z3.simplify(upper - lower)
        identity = z3.IntVal(0 if kind == "sum" else 1, ctx)
        if z3.is_int_value(count) and count.as_long() <= 0:
            return identity
        if index not in get_vars(body):
            if kind == "sum":
                return z3.simplify(z3.If(upper <= lower, identity, count * body))
            if z3.is_true(z3.simplify(body == 1)):
                return identity

        parameters = tuple(sorted((v for v in get_vars(body) if not z3.eq(v, index)),
                                  key=lambda v: v.sexpr()))
        if any(v.sort() != z3.IntSort(ctx) for v in parameters):
            raise AggregateUnsupported("aggregate parameters must be integers")
        # Fresh binders prevent collision with source-level identifiers. The
        # hash uses de Bruijn variables, so alpha-renaming preserves identity.
        canonical = z3.substitute_vars(
            z3.Lambda([index, *parameters], body).body(),
            *[z3.Int(f"__aggregate_key_{i}", ctx) for i in range(len(parameters) + 1)])
        key = kind + canonical.sexpr()
        name = "ari_finite_" + kind + "_" + hashlib.sha256(key.encode()).hexdigest()
        function = z3.Function(name, *([z3.IntSort(ctx)] * (len(parameters) + 3)))
        if function not in self.definitions:
            # SMT simplification need not preserve the syntax/order of a
            # piecewise body after alpha-renaming. Reuse an existing binder
            # only after proving pointwise equality for all its parameters.
            for definition in list(self.definitions.values())[:16]:
                if definition.kind != kind or len(definition.parameters) != len(parameters):
                    continue
                previous = z3.substitute(definition.body,
                    (definition.index, index), *zip(definition.parameters, parameters))
                if _prove(z3.BoolVal(True, ctx), previous == body):
                    return self.expand(definition.function(lower, upper, *parameters))
            self.definitions[function] = AggregateDefinition(
                kind, function, index, parameters, body)
        return self.expand(function(lower, upper, *parameters))

    def expand(self, expression, max_terms=32, budget=256):
        """Only unfold finite small intervals; retain larger symbolic terms."""
        remaining = [budget]
        memo = {}

        def visit(term):
            if term in memo:
                return memo[term]
            if not z3.is_app(term) or remaining[0] <= 0:
                return term
            remaining[0] -= 1
            result = z3.simplify(term.decl()(*[visit(a) for a in term.children()]))
            definition = self.definitions.get(result.decl()) if z3.is_app(result) else None
            if definition:
                lower, upper, *arguments = result.children()
                count = z3.simplify(upper - lower)
                if z3.is_int_value(count) and count.as_long() <= 0:
                    result = z3.IntVal(0 if definition.kind == "sum" else 1, term.ctx)
                elif (z3.is_int_value(count) and count.as_long() <= max_terms
                      and count.as_long() <= remaining[0]):
                    result = z3.IntVal(0 if definition.kind == "sum" else 1, term.ctx)
                    for offset in range(count.as_long()):
                        remaining[0] -= 1
                        value = visit(z3.substitute(definition.body,
                            (definition.index, z3.simplify(lower + offset)),
                            *zip(definition.parameters, arguments)))
                        result = result + value if definition.kind == "sum" else result * value
                    result = z3.simplify(result)
            memo[term] = result
            return result

        return visit(expression)

    def export(self):
        definitions = []
        for definition in self.definitions.values():
            ctx = definition.index.ctx
            lower, upper = z3.FreshInt("aggregate_lower", ctx), z3.FreshInt("aggregate_upper", ctx)
            solver = z3.Solver(ctx=ctx)
            solver.add(definition.function(lower, upper, *definition.parameters) == definition.body)
            solver.add(definition.index == 0)
            definitions.append(dict(kind=definition.kind, smt2=solver.to_smt2()))
        return definitions


class AggregateClosedForm(ExprClosedForm):
    def __init__(self, values, ind_var, aggregates):
        super().__init__(values, ind_var)
        self.aggregates = aggregates


def has_external_inputs(rec):
    states = {lhs.decl() for trans in rec.transitions for lhs in trans}
    return any(z3.is_app(term) and term.num_args() and
               term.decl().kind() == z3.Z3_OP_UNINTERPRETED and term.decl() not in states
               for trans in rec.transitions for rhs in trans.values() for term in _walk(rhs))


def _coefficients(rec, function, solved):
    t = rec.ind_var
    current = function(t)
    marker = z3.FreshInt("aggregate_state", t.ctx)
    states = {lhs.decl() for trans in rec.transitions for lhs in trans}
    guards, coefficients = [], []
    grouped = {}
    for condition, transition in zip(rec.conditions, rec.transitions):
        guard = z3.simplify(z3.substitute(condition, *solved.items()))
        update = next(rhs for lhs, rhs in transition.items() if lhs.decl() == function)
        rhs = z3.simplify(z3.substitute(update, *solved.items()))
        grouped.setdefault(rhs, []).append(guard)
    for rhs, alternatives in grouped.items():
        guard = z3.simplify(z3.Or(*alternatives))
        if _prove(t >= 0, guard):
            guard = z3.BoolVal(True, t.ctx)
        if any(z3.is_app(term) and term.decl() in states for term in _walk(guard)):
            return None
        rhs = z3.substitute(rhs, (current, marker))
        if any(z3.is_app(term) and term.decl() in states for term in _walk(rhs)):
            return None
        beta = z3.simplify(z3.substitute(rhs, (marker, z3.IntVal(0, t.ctx))))
        alpha = z3.simplify(z3.substitute(rhs, (marker, z3.IntVal(1, t.ctx))) - beta)
        if marker in get_vars(alpha) or marker in get_vars(beta):
            return None
        if not _prove(z3.And(t >= 0, guard), rhs == alpha * marker + beta):
            return None
        guards.append(guard)
        coefficients.append((alpha, beta))
    if not _prove(t >= 0, z3.Or(*guards)):
        return None
    alpha, beta = coefficients[-1]
    for guard, (a, b) in reversed(list(zip(guards[:-1], coefficients[:-1]))):
        alpha, beta = z3.If(guard, a, alpha), z3.If(guard, b, beta)
    return z3.simplify(alpha), z3.simplify(beta)


def _solution(t, initial, alpha, beta, aggregates):
    r, q = z3.FreshInt("sum_index", t.ctx), z3.FreshInt("product_index", t.ctx)
    at = lambda expression, index: z3.simplify(z3.substitute(expression, (t, index)))
    if z3.is_true(z3.simplify(alpha == 1)):
        if t not in get_vars(beta):
            return z3.simplify(initial + t * beta)
        return z3.simplify(initial + aggregates.make("sum", r, 0, t, at(beta, r)))
    product = aggregates.make("product", q, 0, t, at(alpha, q))
    if z3.is_true(z3.simplify(beta == 0)):
        return z3.simplify(initial * product)
    suffix = aggregates.make("product", q, r + 1, t, at(alpha, q))
    return z3.simplify(initial * product + aggregates.make("sum", r, 0, t, at(beta, r) * suffix))


def certify_aggregate_closed_form(rec, closed):
    """Reconstruct every component using the trusted finite-affine identity."""
    if not isinstance(rec, LoopRecurrence) or rec.reverses:
        return False
    t = rec.ind_var
    if not z3.is_const(t) or not z3.is_int(t) or any(
            lhs.decl().arity() != 1 or
            not z3.is_true(z3.simplify(lhs.arg(0) == t + 1))
            for trans in rec.transitions for lhs in trans):
        return False
    solved = {}
    pending = dict(closed.as_dict())
    states = {lhs.decl() for trans in rec.transitions for lhs in trans}
    while pending:
        progress = False
        for app, candidate in list(pending.items()):
            initial = rec.initial.get(app.decl()(0))
            if initial is None or any(z3.is_app(term) and term.decl() in states
                                      for term in _walk(initial)):
                continue
            coefficients = _coefficients(rec, app.decl(), solved)
            if coefficients is None:
                continue
            expected = _solution(rec.ind_var, initial, *coefficients, closed.aggregates)
            if not _prove(rec.ind_var >= 0, candidate == expected):
                return False
            solved[app] = candidate
            del pending[app]
            progress = True
        if not progress:
            return False
    return set(solved) == {f(rec.ind_var) for f in states}


def solve_aggregate_recurrence(rec):
    if not isinstance(rec, LoopRecurrence) or rec.reverses:
        return None
    states = {lhs.decl() for trans in rec.transitions for lhs in trans}
    if not states or len(states) > 16 or any(f.arity() != 1 for f in states):
        return None
    t = rec.ind_var
    if not z3.is_const(t) or not z3.is_int(t):
        return None
    if any(not z3.is_true(z3.simplify(lhs.arg(0) == t + 1))
           for trans in rec.transitions for lhs in trans):
        return None
    solved, aggregates = {}, AggregateContext()
    pending = sorted(states, key=lambda f: str(f.name()))
    while pending:
        progress = False
        for function in list(pending):
            initial = rec.initial.get(function(0))
            if initial is None or not z3.is_int(initial) or any(
                    z3.is_app(term) and term.decl() in states for term in _walk(initial)):
                continue
            coefficients = _coefficients(rec, function, solved)
            if coefficients is None:
                continue
            solved[function(t)] = _solution(t, initial, *coefficients, aggregates)
            pending.remove(function)
            progress = True
        if not progress:
            return None
    result = AggregateClosedForm(solved, t, aggregates)
    if not aggregates.definitions or not certify_aggregate_closed_form(rec, result):
        return None
    return result
