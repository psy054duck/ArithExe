"""Sample-guided, quantifier-free upper bounds on finite path phases."""

from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass
from itertools import product
import logging
import time

import z3

from . import utils

logger = logging.getLogger(__name__)
_current_budget = ContextVar("phase_length_budget", default=None)


def solve_integer_phase_length(constraint, variable, timeout_ms=200):
    """Choose an integer interval endpoint; certify coverage without QE."""
    if not z3.is_int(variable):
        return None
    branches = utils.DNFConverter().to_dnf(constraint)
    if len(branches) > 32:
        return None
    solutions = []
    for branch in branches:
        lower, upper = [], []
        for literal in branch:
            try:
                atoms = utils.literal2canonical(z3.simplify(literal))
            except (z3.Z3Exception, AttributeError, TypeError):
                continue
            # Negated equality is a disjunction, not two interval bounds.
            if not atoms or len(atoms) != 1:
                continue
            atom = atoms[0]
            if atom is None or not (z3.is_ge(atom) or z3.is_eq(atom)):
                continue
            difference = z3.simplify(atom.arg(0)-atom.arg(1))
            expressions = [difference, -difference] if z3.is_eq(atom) else [difference]
            for expression in expressions:
                rest = z3.simplify(z3.substitute(expression, (variable, z3.IntVal(0, ctx=variable.ctx))))
                coefficient = z3.simplify(
                    z3.substitute(expression, (variable, z3.IntVal(1, ctx=variable.ctx)))-rest)
                if not z3.is_int_value(coefficient) or not expression.sort().is_int():
                    continue
                if not z3.is_true(z3.simplify(expression == coefficient*variable+rest)):
                    continue
                step = coefficient.as_long()
                if step > 0:
                    lower.append(z3.simplify(-(rest/step)))
                elif step < 0:
                    upper.append(z3.simplify(rest/(-step)))
        choices = lower or upper
        if not choices:
            return None
        candidate = choices[0]
        for value in choices[1:]:
            candidate = z3.If(candidate >= value if lower else candidate <= value,
                              candidate, value)
        candidate = z3.simplify(candidate)
        if variable in utils.get_vars(candidate):
            return None
        original = z3.And(*branch)
        region = z3.simplify(z3.substitute(original, (variable, candidate)))
        solver = z3.Solver(ctx=variable.ctx)
        solver.set(timeout=timeout_ms)
        # Any feasible count must imply that the chosen endpoint is feasible.
        # This proves that substitution loses no feasible parameter region.
        solver.add(original, z3.Not(region))
        if solver.check() != z3.unsat:
            return None
        solutions.append((region, {variable: candidate}))
    coverage = z3.Solver(ctx=variable.ctx)
    coverage.set(timeout=timeout_ms)
    covered = (z3.Or(*[region for region, _ in solutions]) if solutions
               else z3.BoolVal(False, ctx=variable.ctx))
    coverage.add(constraint, z3.Not(covered))
    if coverage.check() != z3.unsat:
        return None
    return solutions


@dataclass(frozen=True)
class PhaseLengthOptions:
    enabled: bool = True
    check_timeout_ms: int = 100
    max_transitions: int = 32
    max_enum_count: int = 8
    max_variants: int = 16
    total_time_ms: int = 1000

    def __post_init__(self):
        for name in ("check_timeout_ms", "max_transitions", "max_enum_count", "max_variants"):
            if not isinstance(getattr(self, name), int) or getattr(self, name) <= 0:
                raise ValueError(name + " must be a positive integer")
        if not isinstance(self.total_time_ms, int) or self.total_time_ms < 0:
            raise ValueError("total_time_ms must be a nonnegative integer")


@dataclass(frozen=True)
class PhasePlan:
    counts: tuple
    bounds: tuple

    @property
    def specialized(self):
        return any(bound is not None for bound in self.bounds)


def count_variables(rec, count, precondition):
    used = {str(var) for var in rec.get_symbolic_values() | utils.get_vars(precondition) | {rec.ind_var}}
    prefix = '_phase_count'
    while any(prefix+str(i) in used for i in range(count)):
        prefix += '_'
    return tuple(z3.Int(prefix+str(i), ctx=rec.ind_var.ctx) for i in range(count))


class PhaseLengthBudget:
    def __init__(self, options=None):
        self.options = options if isinstance(options, PhaseLengthOptions) else PhaseLengthOptions(**(options or {}))
        self.stats = dict(checks=0, proved_bounds=0, cache_hits=0, unknown_checks=0,
                          fixed_counts=0, enumerated_counts=0, templates=0,
                          budget_skips=0, transition_skips=0, variants=0,
                          finite_instantiations=0, qe_calls=0, qe_seconds=0.0,
                          candidate_checks=0, rejected_candidates=0,
                          boundary_templates=0, boundary_candidates=0,
                          boundary_proved=0, boundary_solutions=0,
                          preprocessing_seconds=0.0)
        self._cache = {}
        self._active_since = None

    @property
    def remaining_ms(self):
        elapsed = 0.0 if self._active_since is None else time.monotonic()-self._active_since
        return max(0.0, self.options.total_time_ms -
                   1000*(self.stats["preprocessing_seconds"] + elapsed))

    @contextmanager
    def processing(self):
        # Nested helpers share a single timer rather than charging work twice.
        if self._active_since is not None:
            yield
            return
        self._active_since = time.monotonic()
        try:
            yield
        finally:
            self.stats["preprocessing_seconds"] += time.monotonic()-self._active_since
            self._active_since = None

    def upper_bound(self, rec, sequence, count, precondition):
        if not self.options.enabled or rec.reverses:
            return None
        if not isinstance(count, int) or count < 1 or not sequence:
            return None
        if (count+1)*len(sequence) > self.options.max_transitions:
            self.stats["transition_skips"] += 1
            return None
        if self.remaining_ms < 1:
            self.stats["budget_skips"] += 1
            return None
        conditions, transitions = rec.conditions, rec.transitions
        updates = [{lhs.decl(): rhs for lhs, rhs in trans.items()} for trans in transitions]
        declarations = rec.func_decls
        if any(decl.arity() != 1 for decl in declarations):
            return None
        # Initial-state assumptions are not necessarily invariants at a later
        # phase entry. Only parameter-only assumptions may restrict this proof.
        mentioned = utils.get_applied_functions(precondition)
        if (any(app.decl() in declarations for app in mentioned) or
                rec.ind_var in utils.get_vars(precondition)):
            precondition = z3.BoolVal(True, ctx=rec.ind_var.ctx)
        signature = (rec.ind_var.ctx, tuple(conditions),
                     tuple(tuple(trans.items()) for trans in transitions))
        key = (signature, tuple(sequence), count, precondition)
        if key in self._cache:
            self.stats["cache_hits"] += 1
            return self._cache[key]
        with self.processing():
            entry = z3.FreshInt("phase_entry", ctx=rec.ind_var.ctx)
            state = {decl(rec.ind_var): z3.FreshConst(decl.range(), "phase_state")
                     for decl in declarations}
            guards = [precondition, entry >= 0]
            for step, branch in enumerate(list(sequence)*(count+1)):
                if self.remaining_ms < 1:
                    self.stats["budget_skips"] += 1
                    return None
                mapping = list(state.items()) + [(rec.ind_var, entry+step)]
                guards.append(z3.simplify(z3.substitute(conditions[branch], mapping)))
                state = {decl(rec.ind_var): z3.simplify(z3.substitute(
                    updates[branch][decl], mapping))
                    for decl in declarations}
            timeout = min(self.options.check_timeout_ms, int(self.remaining_ms))
            if timeout < 1:
                self.stats["budget_skips"] += 1
                return None
            solver = z3.Solver(ctx=rec.ind_var.ctx)
            solver.set(timeout=timeout)
            self.stats["checks"] += 1
            result = solver.check(*guards)
            bound = count if result == z3.unsat else None
            self._cache[key] = bound
            if bound is not None:
                self.stats["proved_bounds"] += 1
            elif result == z3.unknown:
                self.stats["unknown_checks"] += 1
            return bound

    def plans(self, rec, pattern, precondition):
        variables = count_variables(rec, len(pattern)-1, precondition)
        bounds = [None]*len(variables)
        self.stats["templates"] += 1
        with self.processing():
            # Never interpret the finite witness of the infinite suffix as a
            # bound. Cheap singleton phases get the shared request budget first.
            order = sorted(range(len(variables)), key=lambda i:
                           (pattern[i][1] != 1, (pattern[i][1]+1)*len(pattern[i][0]), i))
            for i in order:
                bounds[i] = self.upper_bound(rec, *pattern[i], precondition)
            choices = []
            variants = 1
            for variable, bound in zip(variables, bounds):
                if bound == 1:
                    choices.append((1,))
                    self.stats["fixed_counts"] += 1
                elif (bound is not None and bound <= self.options.max_enum_count and
                      variants*bound <= self.options.max_variants):
                    choices.append(tuple(range(1, bound+1)))
                    variants *= bound
                    self.stats["enumerated_counts"] += 1
                else:
                    choices.append((variable,))
            plans = [PhasePlan(tuple(counts), tuple(bounds)) for counts in product(*choices)]
            self.stats["variants"] += len(plans)
            return plans


@contextmanager
def phase_length_scope(options=None, stats=None):
    existing = _current_budget.get()
    if existing is not None and options is None:
        yield existing
        return
    budget = PhaseLengthBudget(options)
    token = _current_budget.set(budget)
    try:
        yield budget
    finally:
        if stats is not None:
            stats.update(budget.stats)
        logger.info("Phase-length statistics: %s", budget.stats)
        _current_budget.reset(token)
