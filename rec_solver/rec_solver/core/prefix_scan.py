"""Certified scalar reduction for zero-based, seeded prefix scans."""

import z3

from . import utils
from .recurrence import LoopRecurrence
from .tupling import certify_closed_form
from .ultimately_periodic import solve_ultimately_periodic_symbolic


def try_prefix_closed_form(checker):
    """Propose a write-diagonal summary and certify all original equations."""
    if len(checker.args) != 2:
        return None
    index, iteration = checker.args
    initial = z3.IntVal(0)
    guards = []
    zero_time = [(iteration, z3.IntVal(0))]
    for case in reversed(checker.rec.get_base_cases()):
        guard = z3.substitute(case.condition, zero_time)
        guards.append(guard)
        initial = z3.If(guard, z3.substitute(case.op[0], zero_time), initial)
    checker.require(z3.BoolVal(True), z3.Or(*guards),
                    "prefix initial values are incomplete")
    initial = z3.simplify(initial)

    seed = z3.IntVal(0)
    seed_guards = []
    for case in reversed(checker.cases):
        guard = checker.at(case.condition, (z3.IntVal(0), z3.IntVal(1)))
        if not checker.feasible(guard):
            continue
        body = checker.rhs(case, (z3.IntVal(0), z3.IntVal(1)))
        body, = checker.reduce_base_calls(guard, (body,))
        if checker.calls(body):
            return None
        seed_guards.append(guard)
        seed = z3.If(guard, body, seed)
    checker.require(z3.BoolVal(True), z3.Or(*seed_guards),
                    "prefix seed is incomplete")
    seed = z3.simplify(seed)

    # On the write diagonal, A(k,k+1) becomes the scalar state G(k).
    t = z3.Int("ari_prefix_t")
    g = z3.Function("ari_prefix_value", z3.IntSort(), z3.IntSort())
    used_names = {str(a) for a in checker.args}
    for case in checker.cases:
        for expression in (case.condition, checker.rhs(case, checker.args)):
            used_names.update(str(v) for v in utils.get_vars(expression))
            used_names.update(str(f.decl().name())
                              for f in utils.get_applied_functions(expression))
    if str(t) in used_names or str(g.name()) in used_names:
        return None

    conditions, transitions = [], []
    domain = t >= 0
    for case in checker.cases:
        guard = checker.at(case.condition, (t + 1, t + 2))
        active = z3.And(domain, guard)
        if not checker.feasible(active):
            continue
        body = checker.rhs(case, (t + 1, t + 2))
        replacements = []
        for call in checker.calls(body):
            if not checker.proves(active, z3.And(call.arg(0) == t,
                                                  call.arg(1) == t + 1)):
                return None
            replacements.append((call, g(t)))
        body = z3.simplify(z3.substitute(body, replacements))
        conditions.append(z3.BoolVal(True) if checker.proves(domain, guard) else guard)
        transitions.append({g(t + 1): body})
    if not transitions:
        return None
    scalar = LoopRecurrence.mk_rec({g(0): seed}, conditions, transitions)
    closed = solve_ultimately_periodic_symbolic(scalar, precondition=domain).as_dict()
    if g(t) not in closed:
        return None
    diagonal = z3.simplify(z3.substitute(closed[g(t)], (t, index)))
    candidate = z3.simplify(z3.If(z3.And(index >= 0, iteration > index),
                                  diagonal, initial))
    certify_closed_form(checker.rec, candidate, checker.options)
    return candidate
