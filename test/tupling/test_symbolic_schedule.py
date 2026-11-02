from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core import ultimately_periodic as periodic
from rec_solver.core.closed_form import PeriodicClosedForm
from rec_solver.core.verification import is_inductive_scalar_closed_form


def reset_recurrence(period=3, increment=3):
    return f"""
i(0) = 0;
s(0) = u;
p(0) = v;
if (i(n) == 0) {{
    i(n+1) = i(n)+1;
    s(n+1) = 0;
    p(n+1) = p(n);
}} else if (And(i(n)%{period} == 0, i(n) >= 0)) {{
    i(n+1) = i(n)+1;
    s(n+1) = s(n)+{increment};
    p(n+1) = p(n);
}} else {{
    i(n+1) = i(n)+1;
    s(n+1) = s(n);
    p(n+1) = p(n);
}}
"""


class SymbolicScheduleTests(unittest.TestCase):
    def test_reset_sum_and_unused_symbolic_state_need_no_qe(self):
        original_then = z3.Then

        def no_qe(*args, **kwargs):
            self.assertNotIn("qe", args, "parameter-independent schedule should not need QE")
            return original_then(*args, **kwargs)

        for period, increment in [(2, 2), (2, 20), (3, 3), (5, 7)]:
            with self.subTest(period=period), patch.object(periodic.z3, "Then", side_effect=no_qe):
                result = solve_str(reset_recurrence(period, increment))
            n, u, v = z3.Ints("n u v")
            expected = {"i": n, "s": z3.If(n == 0, u, increment*((n-1)/period)), "p": v}
            solver = z3.Solver()
            solver.set(timeout=5000)
            solver.add(n >= 0, z3.Or(*[
                expression != expected[str(app.decl().name())]
                for app, expression in result.as_dict().items()]))
            self.assertEqual(solver.check(), z3.unsat)
            self.assertEqual(len(result.as_dict()), 3)

    def test_verified_suffix_does_not_absorb_reset_prefix(self):
        rec = parse_str(reset_recurrence(2, 2)).subs({z3.Int("u"): z3.IntVal(0),
                                                   z3.Int("v"): z3.IntVal(0)})
        _, schedule = periodic._solve_ultimately_periodic_initial(rec)
        self.assertEqual(schedule, [([0], 1), ([2, 1], 1)])
        self.assertIsNotNone(periodic._try_fixed_symbolic_pattern(
            parse_str(reset_recurrence(2, 2)), schedule, z3.BoolVal(True)))

    def branching_recurrence(self):
        return parse_str("""
x(0) = p;
if (x(n) >= 0) { x(n+1) = x(n)+1; }
else { x(n+1) = x(n)-1; }
""")

    def test_sampled_schedule_is_not_assumed_for_all_parameters(self):
        rec = self.branching_recurrence()
        self.assertIsNone(periodic._try_fixed_symbolic_pattern(rec, [([0], 20)], z3.BoolVal(True)))

    def test_schedule_can_be_certified_under_parameter_precondition(self):
        rec = self.branching_recurrence()
        candidate = periodic._try_fixed_symbolic_pattern(rec, [([0], 20)], z3.Int("p") >= 0)
        self.assertIsNotNone(candidate)

    def test_uncertified_schedule_preserves_general_symbolic_fallback(self):
        rec = self.branching_recurrence()
        with patch.object(periodic, "_try_fixed_symbolic_pattern", return_value=None), \
                patch.object(periodic, "_set_up_constraints", side_effect=RuntimeError("general path")):
            with self.assertRaisesRegex(RuntimeError, "general path"):
                periodic.solve_ultimately_periodic_symbolic(rec)

    def test_incorrect_initial_value_is_rejected(self):
        rec = self.branching_recurrence()
        n = rec.ind_var
        x = next(iter(rec.initial)).decl()(n)
        candidate = PeriodicClosedForm([{x: n}], n)
        self.assertFalse(is_inductive_scalar_closed_form(rec, candidate, require_complete=True))

    def test_incorrect_transition_is_rejected(self):
        rec = self.branching_recurrence()
        n = rec.ind_var
        x = next(iter(rec.initial)).decl()(n)
        candidate = PeriodicClosedForm([{x: z3.Int("p")+n}], n)
        self.assertFalse(is_inductive_scalar_closed_form(rec, candidate, require_complete=True))

    def test_incomplete_candidate_is_rejected(self):
        rec = parse_str(reset_recurrence())
        n = rec.ind_var
        candidate = PeriodicClosedForm([{z3.Function("i", z3.IntSort(), z3.IntSort())(n): n}], n)
        self.assertFalse(is_inductive_scalar_closed_form(rec, candidate, require_complete=True))

    def test_unknown_certificate_cannot_accept_schedule(self):
        rec = self.branching_recurrence()
        n = rec.ind_var
        x = next(iter(rec.initial)).decl()(n)
        candidate = PeriodicClosedForm([{x: z3.Int("p")+n}], n)
        with patch("rec_solver.core.verification.z3.Solver") as solver:
            solver.return_value.check.return_value = z3.unknown
            self.assertFalse(is_inductive_scalar_closed_form(rec, candidate, require_complete=True))

    def test_reverse_composition_does_not_use_scalar_certificate(self):
        rec = self.branching_recurrence()
        rec.set_reverse(set(rec.all_funcs))
        with patch.object(periodic, "_compute_solution_by_index_seq", side_effect=AssertionError("not scalar")):
            self.assertIsNone(periodic._try_fixed_symbolic_pattern(rec, [([0], 20)], z3.BoolVal(True)))


if __name__ == "__main__":
    unittest.main()
