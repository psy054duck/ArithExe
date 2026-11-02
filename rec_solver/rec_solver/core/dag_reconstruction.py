"""APS result reconstruction without expanding symbolic base conditions.

Traversal solving and stopping-time computation use the existing backend.
The original pivot implementation retains its existing reconstruction path.
"""

import logging
import z3

from .multivariate import (
    nearly_tail2loop, get_loop_cond, symbol2func,
)
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from . import utils
from .phase_lengths import solve_integer_phase_length

logger = logging.getLogger(__name__)


def _stopping_boundary(counter, terminal, case, closed, precondition):
    candidates = [z3.IntVal(0, ctx=counter.ctx)]
    try:
        pending = list(closed.flatten_conditions)
    except TypeError:
        pending = []
    seen = set()
    while pending and len(candidates) < 32:
        expression = pending.pop()
        if expression.get_id() in seen:
            continue
        seen.add(expression.get_id())
        if not z3.is_app(expression):
            continue
        if (z3.is_le(expression) or z3.is_lt(expression) or z3.is_ge(expression)
                or z3.is_gt(expression) or z3.is_eq(expression)):
            if all(arg.is_int() for arg in expression.children()):
                difference = expression.arg(0)-expression.arg(1)
                rest = z3.simplify(z3.substitute(difference, (counter, z3.IntVal(0, ctx=counter.ctx))))
                coefficient = z3.simplify(z3.substitute(
                    difference, (counter, z3.IntVal(1, ctx=counter.ctx)))-rest)
                if z3.is_int_value(coefficient) and abs(coefficient.as_long()) == 1:
                    boundary = z3.simplify(-coefficient*rest)
                    if counter not in utils.get_vars(boundary):
                        candidates.extend([boundary, boundary+1, boundary-1])
        pending.extend(expression.children())
    solver = z3.Solver(ctx=counter.ctx)
    solver.set(timeout=1000)
    solver.add(precondition, case)
    checked = set()
    for value in candidates:
        value = z3.simplify(value)
        if value.sexpr() in checked:
            continue
        checked.add(value.sexpr())
        obligations = [value >= 0, z3.substitute(terminal, (counter, value)),
                       z3.Implies(z3.And(counter >= 0, counter < value),
                                  z3.Not(terminal))]
        valid = True
        for obligation in obligations:
            solver.push()
            solver.add(z3.Not(obligation))
            valid = solver.check() == z3.unsat
            solver.pop()
            if not valid:
                break
        if valid:
            return value
    return None


def compute_stopping_time(counter, stop, base_guard, state_solution, precondition):
    """Cover every APS parameter region with a certified first stopping time."""
    branches = []
    for case, closed in zip(state_solution._constraints, state_solution._closed_forms):
        terminal = z3.simplify(z3.substitute(base_guard, list(closed.as_dict().items())),
                               elim_ite=True)
        boundary = _stopping_boundary(counter, terminal, case, closed, precondition)
        if boundary is not None:
            branches.append((z3.And(precondition, case), boundary))
            continue
        constraints = z3.And(
            precondition, case, stop >= 0,
            z3.ForAll(counter, z3.Implies(z3.And(counter >= 0, counter < stop),
                                         z3.Not(terminal))),
            z3.substitute(terminal, (counter, stop)))
        projected = z3.TryFor(z3.Then('simplify', 'qe', 'ctx-solver-simplify', 'simplify'),
                             3000)(constraints).as_expr()
        solution = utils.solve_piecewise_sol(projected, [stop], z3.Int,
                                             precondition=precondition)
        if solution is None:
            alternatives = solve_integer_phase_length(projected, stop)
            if alternatives is None:
                raise ValueError('could not determine an exact APS stopping time')
        else:
            alternatives = zip(solution.conditions, solution.expressions)
        for region, values in alternatives:
            value = values[stop]
            condition = z3.simplify(z3.And(precondition, case, region))
            condition = z3.simplify(z3.substitute(condition, (stop, value)))
            if stop in utils.get_vars(condition) or stop in utils.get_vars(value):
                raise ValueError('APS stopping time contains an unresolved count')
            solver = z3.Solver(ctx=counter.ctx)
            solver.set(timeout=1000)
            solver.add(condition, z3.Not(z3.substitute(projected, (stop, value))))
            if solver.check() != z3.unsat:
                raise ValueError('APS stopping-time candidate was not certified')
            branches.append((condition, value))
            logger.debug('APS stopping region %s -> %s', condition, value)
    solver = z3.Solver(ctx=counter.ctx)
    solver.set(timeout=1000)
    solver.add(precondition, z3.Not(z3.Or(*[condition for condition, _ in branches])))
    covered = solver.check()
    if covered != z3.unsat:
        logger.debug('APS uncovered inputs: %s', solver.model() if covered == z3.sat else solver.reason_unknown())
        raise ValueError('APS stopping times do not cover all input regions')
    result = z3.IntVal(0, ctx=counter.ctx)
    for condition, value in reversed(branches):
        result = z3.If(condition, value, result)
    return z3.simplify(result)


def reconstruct_base_return(rec, counter, returns, state_solution, stopping_time):
    """Substitute the terminal state once and preserve base expressions as DAGs."""
    closed = state_solution.as_dict()
    terminal = {app: z3.simplify(z3.substitute(value, (counter, stopping_time)))
                for app, value in closed.items()}
    arguments = [(arg, terminal[symbol2func(arg)(counter)])
                 for arg in rec.func_sig.children()]
    branches = []
    for case in rec.get_base_cases():
        alternatives = case.op if isinstance(case.op, list) else [
            (case.op, z3.BoolVal(True))]
        for ops, alternative_condition in alternatives:
            condition = z3.simplify(z3.substitute(
                z3.And(case.condition, alternative_condition), arguments))
            values = tuple(z3.substitute(op, arguments) for op in ops)
            if len(values) != len(returns):
                raise ValueError("base return dimension differs from APS accumulator")
            branches.append((condition, values))
    if not branches:
        raise ValueError("APS reconstruction requires a terminal base case")

    # The existing stopping-time computation establishes that a base case is
    # reached. As in the original reconstruction, the last branch is default.
    base_values = list(branches[-1][1])
    for condition, values in reversed(branches[:-1]):
        base_values = [z3.If(condition, value, rest)
                       for value, rest in zip(values, base_values)]
    initial_returns = list(zip(returns, base_values))
    return [z3.simplify(z3.substitute(
        terminal[symbol2func(ret)(counter)], initial_returns)) for ret in returns]


def solve_nearly_tail(rec, is_array=False):
    """Use the standard APS solver and reconstruct its return as shared ITEs."""
    assert rec.is_nearly_tail()
    counter, stop = z3.Ints("_d _D")
    returns = z3.Ints(" ".join("_ret%d" % i for i in range(rec.number_ret())))
    scalar = nearly_tail2loop(rec, counter, returns)
    base_guard = get_loop_cond(rec, counter)
    precondition = z3.BoolVal(True)
    if is_array:
        assert rec.func_sig.arity() == 2
        index, iteration = rec.func_sig.children()
        precondition = z3.And(iteration >= 0, index >= 0)
    state_solution = solve_ultimately_periodic_symbolic(scalar, precondition=precondition)
    stopping_time = compute_stopping_time(
        counter, stop, base_guard, state_solution, precondition)
    return reconstruct_base_return(rec, counter, returns, state_solution, stopping_time)
