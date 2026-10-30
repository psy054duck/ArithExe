import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rec_solver"))
import sympy as sp
import z3
from rec_solver.core import utils
from solver import solve_str_to_smt2


class ExactRationalExport(unittest.TestCase):
    def test_exported_gauss_sum_is_exact(self):
        constraints = z3.parse_smt2_string(solve_str_to_smt2(
            "i(0)=1; s(0)=0; if (true) {i(n+1)=i(n)+1; s(n+1)=s(n)+i(n);}"
        ))
        n, s = z3.Ints("n s")
        exact = z3.Function("ari_recurrence_exact", z3.RealSort(), z3.RealSort())
        solver = z3.Solver()
        solver.set(timeout=3000)
        solver.add(constraints, 2 * exact(s) != n * (n + 1))
        self.assertEqual(solver.check(), z3.unsat)
        metadata = [e for e in constraints if "ari_recurrence_exact" in e.sexpr()]
        self.assertTrue(metadata)
        self.assertNotIn("(div ", "\n".join(e.sexpr() for e in metadata))

    def test_rational_constant_is_not_rounded_to_zero(self):
        self.assertEqual(utils.to_z3(sp.Rational(1, 2)), z3.RealVal("1/2"))

    def test_explicit_floor_and_mod_are_preserved(self):
        x = z3.Int("x")
        floor = z3.ToInt(z3.ToReal(x) / 2)
        self.assertTrue(z3.eq(utils.restore_exact_rationals(floor), floor))
        self.assertTrue(z3.eq(utils.restore_exact_rationals(x % 2), x % 2))


if __name__ == "__main__":
    unittest.main()
