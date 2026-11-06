"""Bounded polynomial candidates for tupled, pointwise array histories.

Write domains are template discovery only. Both the tuple equations and the
original scalar recurrence must be certified before a candidate is returned.
"""

import time

import z3

from . import utils
from .logic_simplification import contains_quantifier
from .poly_expr import gen_poly_template


def _project_write_domain(guard, counter):
    if utils.get_applied_functions(guard):
        return None
    time_var = z3.FreshInt("write_time", counter.ctx)
    domain = z3.Exists(time_var, z3.And(time_var > 0, time_var <= counter,
        z3.substitute(guard, (counter, time_var))))
    try:
        domain = z3.TryFor(z3.Then("simplify", "qe", "simplify"), 250)(domain).as_expr()
    except z3.Z3Exception:
        return None
    return None if contains_quantifier(domain) else domain


def _history_templates(checker):
    args = checker.args
    if len(args) < 2 or len(args) > 4:
        return
    for index, counter in enumerate(args):
        previous = list(args)
        previous[index] = counter - 1
        frame = z3.simplify(checker.func(*previous))
        writers, frames = [], []
        for case in checker.rec.get_rec_cases():
            rhs = checker.rhs(case, args)
            if z3.eq(rhs, frame):
                frames.append(case)
            else:
                writers.append(case)
        if not writers or not frames:
            continue
        zero = [(counter, z3.IntVal(0, counter.ctx))]
        base_cases = checker.rec.get_base_cases()
        guards = [z3.simplify(z3.substitute(case.condition, zero))
                  for case in base_cases]
        if not checker.proves(z3.BoolVal(True), z3.Or(*guards)):
            continue
        initial = z3.IntVal(0, counter.ctx)
        for guard, case in reversed(list(zip(guards, base_cases))):
            initial = z3.If(guard, z3.substitute(case.op[0], zero), initial)
        initial = z3.simplify(initial)
        spatial = tuple(a for a in args if not a.eq(counter))
        domain = _project_write_domain(z3.Or(*(case.condition for case in writers)), counter)
        if domain is not None:
            yield spatial, initial, [domain]
        if 2 <= len(writers) <= 4:
            domains = [_project_write_domain(case.condition, counter) for case in writers]
            if all(domain is not None for domain in domains):
                yield spatial, initial, domains


def _tuple_obligations(checker, result, candidate):
    meanings = [checker.at(candidate, component) for component in result.components]
    args = result.recurrence.func_sig.children()
    obligations = []
    for case in result.recurrence.get_base_cases() + result.recurrence.get_rec_cases():
        substitutions = []
        for index, (name, call) in enumerate(getattr(case, "recursive_calls", {}).items()):
            value = z3.substitute(meanings[index], list(zip(args, call.children())))
            substitutions.append((name, value))
        equations = [lhs == z3.substitute(rhs, substitutions)
                     for lhs, rhs in zip(meanings, case.op)]
        obligations.append((case.condition, z3.And(*equations)))
    return obligations


def _synthesize(obligations, coefficients, deadline):
    learner = z3.Solver()
    verifier = z3.Solver()
    for _ in range(24):
        if time.monotonic() >= deadline:
            return None
        learner.set(timeout=100)
        if learner.check() != z3.sat:
            return None
        model = learner.model()
        values = [(c, model.eval(c, model_completion=True)) for c in coefficients]
        counterexample = None
        for guard, equation in obligations:
            if time.monotonic() >= deadline:
                return None
            proposed = z3.simplify(z3.substitute(equation, values))
            if z3.is_true(proposed):
                continue
            verifier.set(timeout=100)
            verifier.push()
            verifier.add(guard, z3.Not(proposed))
            answer = verifier.check()
            if answer == z3.sat:
                witness = verifier.model()
                # Ground the counterexample, not the template coefficients.
                formula = z3.Implies(guard, equation)
                constants = utils.get_vars(formula) - set(coefficients)
                grounded = z3.substitute(formula, [
                    (v, witness.eval(v, model_completion=True)) for v in constants])
                inputs = utils.get_applied_functions(grounded)
                grounded = z3.substitute(grounded, [
                    (v, witness.eval(v, model_completion=True)) for v in inputs])
                counterexample = z3.simplify(grounded)
            verifier.pop()
            if answer == z3.unknown:
                return None
            if counterexample is not None:
                break
        if counterexample is None:
            return values
        learner.add(counterexample)
    return None


def solve_polynomial_array_tuple(checker, result):
    """Propose a bounded polynomial history, with optional per-writer offsets."""
    deadline = time.monotonic() + 2.0
    for spatial, initial, domains in _history_templates(checker):
        for degree in (1, 2):
            if time.monotonic() >= deadline:
                return None
            _, _, monomials = gen_poly_template([utils.to_sympy(a) for a in spatial], degree)
            monomials = sorted(monomials, key=str)
            if len(monomials) > 16:
                continue
            coefficients = [z3.FreshInt("tuple_coefficient") for _ in monomials]
            polynomial = sum(c * utils.to_z3(m) for c, m in zip(coefficients, monomials))
            offsets = [z3.FreshInt("tuple_branch_offset") for _ in domains[1:]]
            candidate = initial
            for domain, offset in reversed(list(zip(domains, [0, *offsets]))):
                candidate = z3.If(domain, polynomial + offset, candidate)
            values = _synthesize(_tuple_obligations(checker, result, candidate),
                                 coefficients + offsets, deadline)
            if values is not None:
                return z3.simplify(z3.substitute(candidate, values))
    return None
