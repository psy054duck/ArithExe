"""Affine external base values and certified loop-invariant input parameters.

This pass changes representation, not solutions: it never proposes an array
summary or an update schedule. Base values are reconstructed by the APS solver.
"""

import sympy as sp
import z3

from . import utils
from .recurrence import BaseCase, RecursiveCase, MultiRecurrence


def _external_apps(expression, func):
    return {app for app in utils.get_applied_functions(expression)
            if app.decl().kind() == z3.Z3_OP_UNINTERPRETED and app.decl() != func}


def _affine(expression):
    # Parsed negative numerals can be unary-minus terms rather than numerals.
    return _affine_normalized(z3.simplify(expression))


def _affine_normalized(expression):
    if z3.is_int_value(expression):
        return True
    if z3.is_const(expression) and expression.sort() == z3.IntSort():
        return expression.decl().kind() == z3.Z3_OP_UNINTERPRETED
    kind = expression.decl().kind()
    if kind in (z3.Z3_OP_ADD, z3.Z3_OP_SUB, z3.Z3_OP_UMINUS):
        return all(_affine_normalized(arg) for arg in expression.children())
    if kind == z3.Z3_OP_MUL:
        return (sum(not z3.is_int_value(arg) for arg in expression.children()) <= 1
                and all(_affine_normalized(arg) for arg in expression.children()))
    return False


class _InputContext:
    def __init__(self, rec, options):
        self.rec = rec
        self.args = tuple(rec.func_sig.children())
        self.func = rec.func_sig.decl()
        self.cases = rec.get_base_cases() + rec.get_rec_cases()
        self.solver = z3.Solver()
        self.solver.set(timeout=options.solver_timeout_ms)
        self.timeout_ms = options.solver_timeout_ms

    def proves(self, premise, conclusion):
        self.solver.push()
        self.solver.add(premise, z3.Not(conclusion))
        result = self.solver.check()
        self.solver.pop()
        return result == z3.unsat

    def require(self, premise, conclusion, message):
        if not self.proves(premise, conclusion):
            from .tupling import TuplingUnsupported
            raise TuplingUnsupported(message)

    def at(self, expression, values):
        return z3.simplify(z3.substitute(expression, list(zip(self.args, values))))


def _normalize_guard_accesses(checker, guard, changing_args):
    """Use equality-gated accesses even inside a negated frame condition."""
    equalities = set()
    pending = [guard]
    while pending:
        atom = pending.pop()
        if z3.is_eq(atom) and all(_affine(arg) for arg in atom.children()):
            equalities.add(atom)
        pending.extend(atom.children())
    changing = set(changing_args)
    for equality in sorted(equalities, key=lambda expression: expression.sexpr()):
        variables = utils.get_vars(equality)
        for argument in changing_args:
            if argument not in variables:
                continue
            symbol = utils.to_sympy(argument)
            solutions = sp.solve(utils.to_sympy(equality.arg(0)-equality.arg(1)),
                                 symbol, dict=True)
            if len(solutions) != 1 or symbol not in solutions[0]:
                continue
            for app in sorted(_external_apps(guard, checker.func), key=lambda term: term.sexpr()):
                candidate = _rewrite_access(app, solutions[0])
                if candidate is None:
                    continue
                if not (utils.get_vars(candidate) & changing) < (utils.get_vars(app) & changing):
                    continue
                proposed = z3.simplify(z3.substitute(guard, (app, candidate)))
                # The equality need not follow from the whole frame guard.
                # Prove equivalence of the Boolean context instead.
                if checker.proves(z3.BoolVal(True), guard == proposed):
                    guard = proposed
    return guard


def _rewrite_access(app, substitution):
    # Rational intermediates are permitted only when they cancel inside the
    # access: k = 2*t-2 implies input(2*t-2) = input(k), not t = k div 2+1.
    arguments = [utils.to_z3(sp.expand(utils.to_sympy(arg).subs(
        substitution, simultaneous=True))) for arg in app.children()]
    if not all(arg.sort() == z3.IntSort() and _affine(arg) for arg in arguments):
        return None
    return z3.simplify(app.decl()(*arguments))


def _merge_base_cases(cases):
    groups = {}
    for case in cases:
        key = tuple(z3.simplify(op).sexpr() for op in case.op)
        if key in groups:
            previous = groups[key]
            groups[key] = BaseCase(z3.Or(previous.condition, case.condition), previous.op)
        else:
            groups[key] = case
    return list(groups.values())


def _simplify_input_guard(checker, guard):
    try:
        proposed = z3.TryFor(z3.Then('simplify', 'ctx-solver-simplify', 'simplify'),
                             checker.timeout_ms)(guard).as_expr()
    except z3.Z3Exception:
        return guard
    if checker.proves(z3.BoolVal(True), proposed == guard):
        return z3.simplify(proposed)
    return guard


def normalize_base_inputs(rec, options):
    """Return an equivalent recurrence and a parameter-to-input restoration map."""
    checker = _InputContext(rec, options)
    args, func = checker.args, checker.func
    invariant_args = [arg for i, arg in enumerate(args) if all(
        checker.proves(case.condition, call.arg(i) == arg)
        for case in checker.rec.get_rec_cases()
        for call in case.recursive_calls.values())]
    changing_args = [arg for arg in args if arg not in invariant_args]
    normalized = []
    cases = _merge_base_cases(rec.get_base_cases()) + rec.get_rec_cases()
    for case in cases:
        guard = z3.simplify(case.condition)
        apps = _external_apps(guard, func)
        if apps:
            guard = _simplify_input_guard(checker, guard)
            apps = _external_apps(guard, func)
            # Existential abstraction is only a simplification proposal. Check
            # equivalence to retain correlations and input-dependent guards.
            fresh = [(app, z3.FreshInt("input_guard")) for app in apps]
            projected = z3.Tactic("qe")(z3.Exists(
                [value for _, value in fresh], z3.substitute(guard, fresh))).as_expr()
            if checker.proves(z3.BoolVal(True), projected == guard):
                guard = z3.simplify(projected)

        if _external_apps(guard, func):
            guard = _normalize_guard_accesses(checker, guard, changing_args)

        # Eliminate changing arguments from external accesses using affine
        # equalities entailed by this branch, without changing recursive maps.
        equalities = []
        todo = [guard]
        while todo:
            atom = todo.pop()
            if z3.is_eq(atom) and all(_affine(a) for a in atom.children()):
                if checker.proves(guard, atom):
                    equalities.append(utils.to_sympy(atom.arg(0) - atom.arg(1)))
            todo.extend(atom.children())
        substitutions = []
        if equalities and changing_args:
            symbols = [utils.to_sympy(arg) for arg in changing_args]
            solutions = sp.solve(equalities, symbols, dict=True)
            if len(solutions) == 1:
                for arg, symbol in zip(changing_args, symbols):
                    if symbol in solutions[0]:
                        value = utils.to_z3(solutions[0][symbol])
                        if value.sort() == z3.IntSort() and _affine(value):
                            if checker.proves(guard, arg == value):
                                substitutions.append((arg, value))
        replacements = []
        for expression in (guard,) + tuple(case.op):
            for app in _external_apps(expression, func):
                if not all(_affine(a) for a in app.children()):
                    raise ValueError("external function arguments must be integer affine expressions")
                value = z3.simplify(z3.substitute(app, substitutions))
                if equalities and changing_args:
                    for equality in equalities:
                        for argument in changing_args:
                            symbol = utils.to_sympy(argument)
                            solutions = sp.solve(equality, symbol, dict=True)
                            if len(solutions) != 1 or symbol not in solutions[0]:
                                continue
                            candidate = _rewrite_access(value, solutions[0])
                            if candidate is not None and checker.proves(guard, value == candidate):
                                value = candidate
                replacements.append((app, value))
        new_guard = z3.simplify(z3.substitute(guard, replacements))
        checker.require(z3.BoolVal(True), guard == new_guard,
                        "input-access normalization changed a branch guard")
        ops = tuple(z3.simplify(z3.substitute(op, replacements)) for op in case.op)
        for before, after in zip(case.op, ops):
            checker.require(guard, before == after, "input-access normalization changed a return value")
        normalized.append((case, new_guard, ops))

    # Only terms used by the scalar traversal need abstraction. External base
    # return values may instead be evaluated at the terminal argument state.
    terms = set()
    for case, guard, ops in normalized:
        terms |= _external_apps(guard, func)
        if isinstance(case, RecursiveCase):
            for op in ops:
                terms |= _external_apps(op, func)
    frozen = []
    expressions = [expression for _, guard, ops in normalized for expression in (guard,)+ops]
    used_names = {variable.decl().name() for expression in expressions
                  for variable in utils.get_vars(expression)}
    used_names |= {app.decl().name() for expression in expressions
                   for app in utils.get_applied_functions(expression)}
    for term in sorted(terms, key=lambda e: e.sexpr()):
        for case in checker.rec.get_rec_cases():
            for call in case.recursive_calls.values():
                checker.require(case.condition, term == checker.at(term, call.children()),
                                "input application varies along the recursive traversal")
        # These parameters cross the SymPy boundary, where Z3's fresh-name
        # punctuation would otherwise be renamed into an unrelated symbol.
        name = 'ari_entry_input_%d' % len(frozen)
        while name in used_names:
            name = '_'+name
        used_names.add(name)
        frozen.append((term, z3.Int(name, ctx=term.ctx)))

    bases, recursive = [], []
    for case, guard, ops in normalized:
        guard = z3.substitute(guard, frozen)
        ops = tuple(z3.substitute(op, frozen) for op in ops)
        if isinstance(case, RecursiveCase):
            calls = {}
            for name, call in case.recursive_calls.items():
                values = [arg if arg in invariant_args else value
                          for arg, value in zip(args, call.children())]
                normalized_call = call.decl()(*values)
                checker.require(case.condition, call == normalized_call,
                                "argument normalization changed a recursive call")
                calls[name] = normalized_call
            recursive.append(RecursiveCase(guard, calls, ops))
        else:
            bases.append(BaseCase(guard, ops))
    return MultiRecurrence(checker.rec.func_sig, bases, recursive), [
        (value, term) for term, value in frozen]
