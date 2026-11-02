"""Bounded, SMT-certified phase counts proposed from affine guard boundaries."""

from itertools import product

import z3

from . import utils


def _check(budget, *constraints):
    timeout = min(budget.options.check_timeout_ms, int(budget.remaining_ms))
    if timeout < 1:
        budget.stats['budget_skips'] += 1
        return z3.unknown
    solver = z3.Solver(ctx=constraints[0].ctx)
    solver.set(timeout=timeout)
    solver.add(*constraints)
    budget.stats['checks'] += 1
    result = solver.check()
    if result == z3.unknown:
        budget.stats['unknown_checks'] += 1
    return result


def _quotient(numerator, divisor):
    numerator = z3.simplify(numerator, som=True)
    terms = numerator.children() if z3.is_add(numerator) else [numerator]
    exact, rest = [], []
    for term in terms:
        factors = term.children() if z3.is_mul(term) else [term]
        coefficient, others = 1, []
        for factor in factors:
            if z3.is_int_value(factor):
                coefficient *= factor.as_long()
            else:
                others.append(factor)
        if coefficient % divisor == 0:
            value = z3.IntVal(coefficient//divisor, ctx=numerator.ctx)
            for factor in others:
                value *= factor
            exact.append(value)
        else:
            rest.append(term)
    residual = z3.Sum(*rest) if rest else z3.IntVal(0, ctx=numerator.ctx)
    return z3.simplify(z3.Sum(*(exact+[residual/divisor])))


def _boundaries(guard, counter, limit):
    pending, seen, values, used = [guard], set(), [], set()
    zero = z3.IntVal(0, ctx=counter.ctx)
    one = z3.IntVal(1, ctx=counter.ctx)
    while pending and len(values) < limit and len(seen) < 512:
        atom = pending.pop()
        if atom.get_id() in seen:
            continue
        seen.add(atom.get_id())
        if not z3.is_app(atom):
            continue
        if ((z3.is_le(atom) or z3.is_lt(atom) or z3.is_ge(atom)
             or z3.is_gt(atom) or z3.is_eq(atom))
                and all(arg.is_int() for arg in atom.children())):
            difference = z3.simplify(atom.arg(0)-atom.arg(1))
            rest = z3.simplify(z3.substitute(difference, (counter, zero)))
            slope = z3.simplify(z3.substitute(difference, (counter, one))-rest)
            if (z3.is_int_value(slope) and slope.as_long() != 0
                    and z3.is_true(z3.simplify(difference == slope*counter+rest))
                    and counter not in utils.get_vars(rest)):
                a = slope.as_long()
                roots = ([-_quotient(rest, a), _quotient(-rest, a)] if a > 0 else
                         [-_quotient(-rest, -a), _quotient(rest, -a)])
                for boundary in roots:
                    for offset in (0, -1, 1):
                        value = z3.simplify(boundary+offset)
                        if value.sexpr() not in used:
                            values.append(value)
                            used.add(value.sexpr())
                            if len(values) >= limit:
                                break
                    if len(values) >= limit:
                        break
        pending.extend(atom.children())
    return values


def _alternatives(guard, limit):
    remaining = [512]

    def visit(expr, negative=False, depth=0):
        remaining[0] -= 1
        if remaining[0] < 0 or depth > 64:
            return None
        if z3.is_not(expr):
            return visit(expr.arg(0), not negative, depth+1)
        if z3.is_and(expr) or z3.is_or(expr):
            conjunction = z3.is_and(expr) != negative
            result = [[]] if conjunction else []
            for child in expr.children():
                part = visit(child, negative, depth+1)
                if part is None:
                    return None
                if conjunction:
                    if len(result)*len(part) > limit:
                        return None
                    result = [left+right for left in result for right in part]
                else:
                    result.extend(part)
                    if len(result) > limit:
                        return None
            return result
        return [[z3.Not(expr) if negative else expr]]

    parts = visit(guard)
    return [z3.And(*part) for part in parts] if parts is not None else [guard]


def infer_guard_phase_lengths(guards, counts, samples, bounds, precondition, budget):
    """Return certified parameter regions and complete finite-count mappings.

    Each guard is the conjunction of all residue guards of a repeated word.
    The last guard is an infinite suffix, never assigned a finite length.
    """
    if (not budget.options.enabled or budget.remaining_ms < 1
            or len(guards) != len(counts)+1):
        return []
    budget.stats['boundary_templates'] += 1
    with budget.processing():
        states = [(precondition, {})]
        symbols = {count for count in counts if not isinstance(count, int)}
        for index, count in enumerate(counts):
            next_states = []
            counter, original = guards[index]
            for region, mapping in states:
                guard = z3.simplify(z3.substitute(original, list(mapping.items())))
                if utils.get_vars(guard) & symbols:
                    continue
                fixed = isinstance(count, int)
                choices = ([z3.IntVal(count, ctx=counter.ctx)] if fixed else
                           _boundaries(guard, counter, budget.options.max_variants))
                if not fixed and 0 < samples[index] <= budget.options.max_enum_count:
                    choices.append(z3.IntVal(samples[index], ctx=counter.ctx))
                covered, seen = z3.BoolVal(False, ctx=counter.ctx), set()
                for value in choices:
                    if budget.remaining_ms < 1 or len(next_states) >= budget.options.max_variants:
                        break
                    value = z3.simplify(value)
                    if value.sexpr() in seen or utils.get_vars(value) & (symbols | {counter}):
                        continue
                    seen.add(value.sexpr())
                    entry = z3.substitute(guard, (counter, z3.IntVal(0, ctx=counter.ctx)))
                    end = z3.substitute(guard, (counter, value if not fixed else value-1))
                    entries = _alternatives(entry, budget.options.max_variants)
                    exits = _alternatives(end if fixed else z3.Not(end), budget.options.max_variants)
                    for entry_case, exit_case in product(entries, exits):
                        if budget.remaining_ms < 1 or len(next_states) >= budget.options.max_variants:
                            break
                        budget.stats['boundary_candidates'] += 1
                        proposal = z3.simplify(z3.And(region, value >= 1,
                                                    entry_case, exit_case))
                        if bounds[index] is not None:
                            proposal = z3.And(proposal, value <= bounds[index])
                        if _check(budget, proposal, z3.Not(covered)) != z3.sat:
                            continue
                        # A free cycle variable is a counterexample witness,
                        # not a universal quantifier to eliminate.
                        if _check(budget, proposal, counter >= 0, counter < value,
                                  z3.Not(guard)) != z3.unsat:
                            continue
                        updated = dict(mapping)
                        if not fixed:
                            updated[count] = value
                        # Keep overlapping proofs simple; discard an older
                        # region only after proving the new one covers it.
                        next_states = [(old_region, old_mapping)
                                       for old_region, old_mapping in next_states
                                       if _check(budget, old_region, z3.Not(proposal)) != z3.unsat]
                        next_states.append((proposal, updated))
                        covered = z3.Or(covered, proposal)
                        budget.stats['boundary_proved'] += 1
            states = next_states
            if not states:
                return []
        results = []
        counter, original = guards[-1]
        for region, mapping in states:
            guard = z3.simplify(z3.substitute(original, list(mapping.items())))
            if utils.get_vars(guard) & symbols:
                continue
            proposal = z3.simplify(z3.And(region, z3.substitute(
                guard, (counter, z3.IntVal(0, ctx=counter.ctx)))))
            if (_check(budget, proposal) == z3.sat
                    and _check(budget, proposal, counter >= 0, z3.Not(guard)) == z3.unsat):
                results.append((proposal, mapping))
        return results


def uncovered_region(precondition, conditions, budget):
    """Only suppress fallback when a bounded coverage proof succeeds."""
    if not conditions:
        return precondition
    region = z3.simplify(z3.And(precondition, z3.Not(z3.Or(*conditions))))
    with budget.processing():
        if _check(budget, region) == z3.unsat:
            return z3.BoolVal(False, ctx=precondition.ctx)
    return region
