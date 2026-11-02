"""Small, induction-certified floor/min candidates for periodic counters."""

import time

import z3

from .closed_form import ExprClosedForm
from .utils import get_vars
from .verification import is_inductive_scalar_closed_form


def _walk(expression):
    pending, seen = [expression], set()
    while pending:
        term = pending.pop()
        if term.get_id() in seen:
            continue
        seen.add(term.get_id())
        yield term
        pending.extend(term.children())


def solve_periodic_counter(rec, budget_ms=2000):
    """Proposals are accepted only after checking every original transition."""
    if rec.reverses or not z3.is_int(rec.ind_var):
        return None
    t = rec.ind_var
    states = set(rec.func_decls)
    if len(states) > 8 or any(f.arity() != 1 for f in states):
        return None
    solved, remaining = {}, []
    for function in sorted(states, key=str):
        initial = rec.initial.get(function(0))
        if initial is None or not z3.is_int(initial):
            return None
        current = function(t)
        updates = [transition.get(function(t+1), current) for transition in rec.transitions]
        deltas = [z3.simplify(rhs-current) for rhs in updates]
        if all(z3.is_int_value(d) and z3.eq(d, deltas[0]) for d in deltas):
            solved[current] = initial + t*deltas[0]
        elif all(z3.is_true(z3.simplify(rhs == initial)) for rhs in updates):
            solved[current] = initial
        else:
            if not z3.is_true(z3.simplify(initial == 0)) or any(
                    not z3.is_int_value(d) or d.as_long() not in (0, 1) for d in deltas):
                return None
            remaining.append(current)
    if len(remaining) != 1:
        return None
    counter, = remaining
    guards = [z3.simplify(z3.substitute(c, *solved.items())) for c in rec.conditions]
    periods, lower_bounds, upper_bounds = set(), {0}, set()
    parameters = set()
    for guard in guards:
        parameters.update(v for v in get_vars(guard) if not z3.eq(v, t))
        for term in _walk(guard):
            if z3.is_app_of(term, z3.Z3_OP_MOD) and z3.is_int_value(term.arg(1)):
                period = term.arg(1).as_long()
                if 1 <= period <= 16:
                    periods.add(period)
            if not (z3.is_eq(term) or z3.is_le(term) or z3.is_lt(term)
                    or z3.is_ge(term) or z3.is_gt(term)):
                continue
            if not z3.is_int(term.arg(0)):
                continue
            difference = term.arg(0)-term.arg(1)
            base = z3.simplify(z3.substitute(difference, (t, z3.IntVal(0, t.ctx))))
            slope = z3.simplify(z3.substitute(difference, (t, z3.IntVal(1, t.ctx)))-base)
            if not (z3.is_int_value(base) and z3.is_int_value(slope)) or not slope.as_long():
                continue
            boundary = (-base.as_long()) // slope.as_long()
            if 0 <= boundary <= 1000000000:
                lower_bounds.update((boundary, boundary+1))
                upper_bounds.update((boundary, boundary+1))
    if len(parameters) > 4 or len(lower_bounds) > 16:
        return None
    periods = sorted(periods) or [1]
    caps = [v for v in sorted(parameters, key=str) if z3.is_int(v)] + [None]
    deadline = time.monotonic() + budget_ms/1000
    starts = sorted(lower_bounds, reverse=True)
    increment_guards = []
    for guard, transition in zip(guards, rec.transitions):
        delta = z3.simplify(transition.get(counter.decl()(t+1), counter)-counter)
        if z3.is_int_value(delta) and delta.as_long() == 1:
            increment_guards.append(z3.substitute(guard, (counter, z3.IntVal(0, t.ctx))))
    if increment_guards:
        proposal = z3.Optimize(ctx=t.ctx)
        proposal.set(timeout=100)
        proposal.add(t >= 0, z3.Or(*increment_guards))
        proposal.minimize(t)
        if proposal.check() == z3.sat:
            first = proposal.model().eval(t)
            if z3.is_int_value(first):
                starts = [first.as_long(), *[v for v in starts if v != first.as_long()]]
    attempts = 0
    for period in periods:
        for lower in starts:
            for residue in range(period):
                # Count points in [lower,t) congruent to residue modulo period.
                raw = (t+period-1-residue)/period - (lower+period-1-residue)//period
                positive = z3.If(raw > 0, raw, 0)
                for cap in caps:
                    for upper in [None, *sorted(upper_bounds, reverse=True)[:8]]:
                        count = positive
                        if upper is not None:
                            limit = (upper+period-1-residue)//period - (lower+period-1-residue)//period
                            limit = max(0, limit)
                            count = z3.If(positive < limit, positive, limit)
                        value = count if cap is None else z3.If(cap <= 0, 0,
                            z3.If(count < cap, count, cap))
                        if time.monotonic() >= deadline or attempts >= 96:
                            return None
                        attempts += 1
                        result = ExprClosedForm({**solved, counter: z3.simplify(value)}, t)
                        if is_inductive_scalar_closed_form(rec, result,
                                timeout_ms=100, require_complete=True):
                            return result
    return None
