from pathlib import Path
import sys
import unittest

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core.recurrence import LoopRecurrence


class ExternalArrayGuardTests(unittest.TestCase):
    def setUp(self):
        self.n = z3.Int("n")
        self.i = z3.Function("i", z3.IntSort(), z3.IntSort())
        self.a = z3.Function("input_array", z3.IntSort(), z3.IntSort())
        self.guard = self.a(self.i(self.n)) < z3.Int("N")

    def test_external_array_index_is_not_an_induction_argument(self):
        rec = LoopRecurrence({self.i(0): z3.IntVal(0)}, [
            (self.guard, {self.i(self.n + 1): self.i(self.n) + 1}),
            (z3.Not(self.guard), {self.i(self.n + 1): 1 + self.i(self.n)}),
        ])
        self.assertTrue(z3.eq(rec.ind_var, self.n))
        self.assertEqual(rec.func_decls, {self.i})
        self.assertEqual(len(rec.conditions), 1)
        self.assertTrue(z3.is_true(rec.conditions[0]))

    def test_missing_else_retains_identity_transition(self):
        rec = LoopRecurrence({self.i(0): z3.IntVal(0)}, [
            (self.guard, {self.i(self.n + 1): self.i(self.n) + 1}),
        ])
        self.assertEqual(len(rec.transitions), 2)
        self.assertTrue(z3.eq(rec.transitions[1][z3.simplify(self.i(self.n + 1))], self.i(self.n)))
        solver = z3.Solver()
        solver.add(self.i(self.n) == 0, z3.Not(self.guard))
        self.assertEqual(solver.check(), z3.sat)

    def test_different_updates_remain_conditional(self):
        rec = LoopRecurrence({self.i(0): z3.IntVal(0)}, [
            (self.guard, {self.i(self.n + 1): self.i(self.n) + 1}),
            (z3.Not(self.guard), {self.i(self.n + 1): self.i(self.n) - 1}),
        ])
        self.assertEqual(len(rec.conditions), 2)

    def test_real_second_loop_scalar_recurrence_is_solved(self):
        text = """
i(0) = 0;
upper(0) = bound;
if (upper(n) > input_array(i(n))) {
    i(n+1) = i(n)+1;
    upper(n+1) = bound;
} else {
    i(n+1) = i(n)+1;
    upper(n+1) = bound;
}
"""
        rec = parse_str(text)
        result = solve_str(text).as_dict()
        self.assertIn(self.i(rec.ind_var), result)
        upper = z3.Function("upper", z3.IntSort(), z3.IntSort())
        expected = [(self.i(rec.ind_var), rec.ind_var),
                    (upper(rec.ind_var), z3.Int("bound"))]
        solver = z3.Solver()
        solver.set(timeout=5000)
        solver.add(rec.ind_var >= 0, z3.Or(*[
            z3.substitute(app, expected) != z3.substitute(value, expected)
            for app, value in result.items()]))
        self.assertEqual(solver.check(), z3.unsat)


if __name__ == "__main__":
    unittest.main()
