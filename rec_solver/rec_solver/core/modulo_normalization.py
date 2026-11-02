"""Exact, bounded residue splitting for integer modulo in state updates."""

from copy import copy
from contextlib import contextmanager
from contextvars import ContextVar
import logging

import z3

logger = logging.getLogger(__name__)
DEFAULT_MAX_BRANCHES = 128
_branch_limit = ContextVar("modulo_branch_limit", default=DEFAULT_MAX_BRANCHES)


@contextmanager
def modulo_branch_limit(limit):
    if not isinstance(limit, int) or limit < 0:
        raise ValueError("modulo branch limit must be a nonnegative integer")
    token = _branch_limit.set(limit)
    try:
        yield
    finally:
        _branch_limit.reset(token)


def _first_constant_modulo(expressions):
    seen = set()

    def visit(expr):
        if expr in seen:
            return None
        seen.add(expr)
        for child in expr.children():
            found = visit(child)
            if found is not None:
                return found
        if expr.decl().kind() == z3.Z3_OP_MOD:
            divisor = z3.simplify(expr.arg(1))
            if z3.is_int_value(divisor):
                modulus = abs(divisor.as_long())
                if modulus:
                    return expr, modulus
        return None

    for expr in expressions:
        found = visit(expr)
        if found is not None:
            return found
    return None


def normalize_constant_modulo(rec, max_branches=None, timeout_ms=100):
    """Move constant-divisor modulo from updates to exhaustive residue guards.

    Initial values and guard-only modulo are unchanged. An inconclusive
    feasibility query retains its branch; exceeding the limit cancels the
    entire normalization, never truncating the recurrence. Zero disables it.
    """
    if max_branches is None:
        max_branches = _branch_limit.get()
    if not isinstance(max_branches, int) or max_branches < 0:
        raise ValueError("modulo branch limit must be a nonnegative integer")
    if timeout_ms <= 0:
        raise ValueError("modulo feasibility timeout must be positive")
    if max_branches == 0:
        return rec
    original = list(zip(rec.conditions, rec.transitions))
    if not any(_first_constant_modulo(trans.values()) is not None for _, trans in original):
        return rec

    pending = original[::-1]
    branches = []
    checker = z3.Solver(ctx=rec.ind_var.ctx)
    checker.set(timeout=timeout_ms)
    while pending:
        guard, trans = pending.pop()
        trans = {lhs: z3.simplify(rhs) for lhs, rhs in trans.items()}
        found = _first_constant_modulo(trans.values())
        if found is None:
            branches.append((guard, trans))
            continue
        term, modulus = found
        if len(branches) + len(pending) + modulus > max_branches:
            logger.warning("Constant-modulo normalization skipped: branch limit %d exceeded",
                           max_branches)
            return rec
        children = []
        for residue in range(modulus):
            value = z3.IntVal(residue, ctx=term.ctx)
            condition = z3.simplify(z3.And(guard, term == value))
            if checker.check(condition) == z3.unsat:
                continue
            updates = {lhs: z3.substitute(rhs, (term, value)) for lhs, rhs in trans.items()}
            children.append((condition, updates))
        pending.extend(reversed(children))

    # Copy the existing recurrence rather than reinterpreting its exclusive
    # guards or losing reverse-composition and partial-solution metadata.
    result = copy(rec)
    result._conditions = [guard for guard, _ in branches]
    result._transitions = [trans for _, trans in branches]
    result._initial = rec.initial
    result._closed_forms = rec.closed_forms
    result._func_decls = rec.func_decls
    result.reverses = set(rec.reverses)
    return result
