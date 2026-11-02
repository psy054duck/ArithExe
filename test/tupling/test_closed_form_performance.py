from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver.core.closed_form import PeriodicClosedForm, PiecewiseClosedForm, SymbolicClosedForm
from rec_solver.core.logic_simplification import DNFConverter


class ClosedFormExportTests(unittest.TestCase):
    def setUp(self):
        self.n = z3.Int("n")
        self.x = z3.Function("x", z3.IntSort(), z3.IntSort())(self.n)
        self.y = z3.Function("y", z3.IntSort(), z3.IntSort())(self.n)

    def equivalent(self, actual, expected):
        solver = z3.Solver(ctx=actual.ctx)
        solver.add(actual != expected)
        self.assertEqual(solver.check(), z3.unsat)

    def periodic(self, offset=0):
        return PeriodicClosedForm([{self.x: self.n + offset, self.y: 2*self.n}], self.n)

    def symbolic(self, child=None):
        child = child or self.periodic()
        piece = PiecewiseClosedForm(self.n, [z3.BoolVal(True)], [child])
        return SymbolicClosedForm([z3.BoolVal(True)], [piece], self.n)

    def test_repeated_exports_do_not_repeat_arithmetic_simplification(self):
        form = self.periodic()
        with patch("rec_solver.core.closed_form.utils.sp_z3_simplify", side_effect=lambda e, c: e) as simplify:
            first, second = form.as_dict(), form.as_dict()
        self.assertEqual(simplify.call_count, 2)
        self.assertIsNot(first, second)
        self.equivalent(first[self.x], second[self.x])

    def test_exported_dictionary_cannot_poison_the_cache(self):
        form = self.periodic()
        result = form.as_dict()
        result[self.x] = z3.IntVal(999)
        result.pop(self.y)
        self.equivalent(form.as_dict()[self.x], self.n)
        self.assertIn(self.y, form.as_dict())

    def test_public_periodic_dictionary_mutation_invalidates_export(self):
        form = self.periodic()
        form.as_dict()
        form.closed_forms[0][self.x] = self.n + 100
        self.equivalent(form.as_dict()[self.x], self.n + 100)

    def test_constructor_condition_list_mutation_invalidates_export(self):
        conditions = [self.n < 0, self.n >= 0]
        form = PeriodicClosedForm([{self.x: z3.IntVal(1)}, {self.x: z3.IntVal(2)}],
                                  self.n, conditions)
        first = form.as_dict()
        conditions[0] = self.n < 3
        self.equivalent(first[self.x], z3.If(self.n < 0, 1, 2))
        self.equivalent(form.as_dict()[self.x], z3.If(self.n < 3, 1, 2))

    def test_piecewise_export_notices_nested_dictionary_mutation(self):
        child = self.periodic()
        piece = PiecewiseClosedForm(self.n, [z3.BoolVal(True)], [child])
        piece.as_dict()
        child.closed_forms[0][self.x] = self.n + 3
        self.equivalent(piece.as_dict()[self.x], self.n + 3)

    def test_symbolic_export_notices_nested_mutation(self):
        child = self.periodic()
        form = self.symbolic(child)
        form.as_dict()
        child.closed_forms[0][self.x] = self.n - 7
        self.equivalent(form.as_dict()[self.x], self.n - 7)

    def test_piecewise_condition_mutation_invalidates_parent_export(self):
        piece = PiecewiseClosedForm(self.n, [self.n < 0, self.n >= 0],
                                    [self.periodic(1), self.periodic(2)])
        form = SymbolicClosedForm([z3.BoolVal(True)], [piece], self.n)
        form.as_dict()
        piece.conditions[0] = self.n < 3
        self.equivalent(form.as_dict()[self.x], z3.If(self.n < 3, self.n + 1, self.n + 2))

    def test_symbolic_condition_mutation_invalidates_export(self):
        pieces = [PiecewiseClosedForm(self.n, [z3.BoolVal(True)], [self.periodic(i)])
                  for i in [1, 2]]
        conditions = [self.n < 0, self.n >= 0]
        form = SymbolicClosedForm(conditions, pieces, self.n)
        form.as_dict()
        conditions[0] = self.n < 3
        expected = z3.If(conditions[0], pieces[0].as_dict()[self.x], pieces[1].as_dict()[self.x])
        self.equivalent(form.as_dict()[self.x], expected)

    def test_parent_export_simplifies_each_child_value_only_once(self):
        form = self.symbolic()
        with patch("rec_solver.core.closed_form.utils.sp_z3_simplify", side_effect=lambda e, c: e) as simplify:
            first = form.as_dict()
            second = form.as_dict()
            form.as_dict()
        self.assertEqual(simplify.call_count, 2)
        self.equivalent(first[self.x], second[self.x])

    def test_constructor_export_and_substitution_do_not_flatten_eagerly(self):
        with patch.object(SymbolicClosedForm, "_simplify", side_effect=AssertionError("eager flattening")):
            form = self.symbolic()
            self.equivalent(form.as_dict()[self.x], self.n)
            changed = form.subs({self.n: self.n + 2})
            self.equivalent(changed.as_dict()[self.x], self.n + 2)

    def test_flattening_is_lazy_and_reused(self):
        form = self.symbolic()
        with patch.object(form, "_simplify", wraps=form._simplify) as simplify:
            conditions = form.flatten_conditions
            closed = form.flatten_closed_forms
            form.flatten_conditions
        self.assertEqual(simplify.call_count, 1)
        self.assertEqual(len(conditions), len(closed))
        self.equivalent(closed[0][self.x], self.n)

    def test_flattened_view_notices_nested_mutation(self):
        child = self.periodic()
        form = self.symbolic(child)
        form.flatten_closed_forms
        child.closed_forms[0][self.x] = self.n + 4
        self.equivalent(form.flatten_closed_forms[0][self.x], self.n + 4)

    def test_merged_guards_do_not_require_dnf_conversion(self):
        child = self.periodic()
        pieces = [PiecewiseClosedForm(self.n, [z3.BoolVal(True)], [child]) for _ in range(2)]
        with patch.object(DNFConverter, "to_dnf", side_effect=AssertionError("DNF expansion")):
            form = SymbolicClosedForm([self.n < 0, self.n >= 0], pieces, self.n)
            self.equivalent(z3.Or(*form.flatten_conditions), z3.BoolVal(True))
            self.assertEqual(len(form.flatten_closed_forms), 1)
            self.equivalent(form.as_dict()[self.x], self.n)

    def test_integer_division_and_negative_values_keep_exact_meaning(self):
        expr = (self.n + 1) / 3
        form = PeriodicClosedForm([{self.x: expr}], self.n)
        self.equivalent(form.as_dict()[self.x], expr)
        self.equivalent(form.as_dict()[self.x], expr)
        self.equivalent(form.eval_at(-5)[z3.substitute(self.x, (self.n, z3.IntVal(-5)))],
                        z3.IntVal(-2))

    def test_caches_are_isolated_across_z3_contexts(self):
        for context in [z3.Context(), z3.Context()]:
            n = z3.Int("n", ctx=context)
            x = z3.Function("x", z3.IntSort(context), z3.IntSort(context))(n)
            form = PeriodicClosedForm([{x: n + 1}], n)
            with patch("rec_solver.core.closed_form.utils.sp_z3_simplify", side_effect=lambda e, c: e):
                self.equivalent(form.as_dict()[x], n + 1)
                self.equivalent(form.as_dict()[x], n + 1)


if __name__ == "__main__":
    unittest.main()
