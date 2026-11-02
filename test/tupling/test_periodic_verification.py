from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver.rec_parser import parse_str
from rec_solver.core import ultimately_periodic as periodic


class PeriodicVerificationTests(unittest.TestCase):
    def setUp(self):
        self.brs3 = parse_str("""
i(0) = 1;
s(0) = 0;
if (And(i(n)%3 == 0, i(n) >= 0)) {
    i(n+1) = i(n)+1;
    s(n+1) = s(n)+3;
} else {
    i(n+1) = i(n)+1;
    s(n+1) = s(n);
}
""")
        self.prefix = parse_str("""
i(0) = 0;
s(0) = 0;
if (i(n) < 4) {
    i(n+1) = i(n)+1;
    s(n+1) = s(n)+1;
} else if (i(n)%3 == 1) {
    i(n+1) = i(n)+1;
    s(n+1) = s(n)+3;
} else {
    i(n+1) = i(n)+1;
    s(n+1) = s(n);
}
""")

    def candidate(self, rec, pattern):
        return periodic._compute_solution_by_index_seq(rec, pattern)

    def test_brs3_period_three_after_two_step_prefix(self):
        pattern = [([1], 2), ([0, 1, 1], 6)]
        self.assertIsNone(periodic.verify(self.brs3, self.candidate(self.brs3, pattern), pattern))

    def test_prefix_length_includes_sequence_lengths(self):
        pattern = [([0, 0], 2), ([1, 2, 2], 3)]
        self.assertIsNone(periodic.verify(self.prefix, self.candidate(self.prefix, pattern), pattern))

    def test_invalid_period_reports_absolute_first_violation(self):
        pattern = [([0, 0], 2), ([2, 1, 2], 3)]
        self.assertEqual(periodic.verify(self.prefix, self.candidate(self.prefix, pattern), pattern), 4)

    def test_resampled_candidate_keeps_global_iteration_offset(self):
        pattern = [([0], 1), ([1, 1, 0], 3)]
        initial = {app: z3.IntVal(3 if str(app.decl().name()) == "i" else 0)
                   for app in self.brs3.initial}
        rec = self.brs3.copy_rec_with_diff_initial(initial)
        candidate = self.candidate(rec, pattern)
        shifted = candidate.subs({rec.ind_var: rec.ind_var - 2})
        self.assertIsNone(periodic.verify(self.brs3, shifted, pattern, start_offset=2))

    def test_unknown_verification_is_not_success(self):
        pattern = [([1], 2), ([0, 1, 1], 6)]
        candidate = self.candidate(self.brs3, pattern)
        with patch.object(periodic.z3, "Solver") as solver:
            solver.return_value.check.return_value = z3.unknown
            with self.assertRaises(periodic.UnsolvableError):
                periodic.verify(self.brs3, candidate, pattern)

    def test_exhausted_search_rejects_unverified_candidate(self):
        pattern = [([1], 2), ([0, 1, 1], 6)]
        candidate = self.candidate(self.brs3, pattern)
        with patch.object(periodic, "_compute_candidate_solution", return_value=(candidate, pattern)), \
                patch.object(periodic, "verify", return_value=10):
            with self.assertRaises(periodic.UnsolvableError):
                periodic._solve_ultimately_periodic_initial(self.brs3, bnd=20)

    def test_actual_invalid_period_is_not_returned_at_bound(self):
        pattern = [([1, 0], 10)]
        candidate = self.candidate(self.brs3, pattern)
        with patch.object(periodic, "_compute_candidate_solution", return_value=(candidate, pattern)):
            with self.assertRaises(periodic.UnsolvableError):
                periodic._solve_ultimately_periodic_initial(self.brs3, bnd=20)

    def test_symbolic_sampling_honors_search_bound_and_solver_option(self):
        initial = {app: z3.Int("initial_sum") if str(app.decl().name()) == "s" else value
                   for app, value in self.brs3.initial.items()}
        rec = self.brs3.copy_rec_with_diff_initial(initial)
        with patch.object(periodic, "_solve_ultimately_periodic_initial",
                          side_effect=periodic.UnsolvableError("bound exhausted")) as solve:
            with self.assertRaises(periodic.UnsolvableError):
                periodic.solve_ultimately_periodic_symbolic(rec, bnd=20, enable_bounded_cfinite=False)
        self.assertEqual(solve.call_args.args[1], 20)
        self.assertFalse(solve.call_args.kwargs["enable_bounded_cfinite"])

    def test_empty_search_budget_rejects(self):
        with self.assertRaises(periodic.UnsolvableError):
            periodic._solve_ultimately_periodic_initial(self.brs3, bnd=10)

    def test_candidate_failing_immediately_is_rejected(self):
        with patch.object(periodic, "verify", return_value=0):
            with self.assertRaises(periodic.UnsolvableError):
                periodic._solve_ultimately_periodic_initial(self.brs3)

    def test_retry_keeps_only_verified_prefix_and_preserves_solution(self):
        verify = periodic.verify
        compute = periodic._compute_candidate_solution
        attempts = []

        def reject_first(*args, **kwargs):
            attempts.append(kwargs["start_offset"])
            return 10 if len(attempts) == 1 else verify(*args, **kwargs)

        def candidate_after_retry(rec, start, *args, **kwargs):
            if start == 0:
                return compute(rec, start, *args, **kwargs)
            initial = {app.decl()(0): value for app, value in kwargs["start_value"].items()}
            pattern = [([1], 1), ([0, 1, 1], 13)]
            return self.candidate(rec.copy_rec_with_diff_initial(initial), pattern), pattern

        with patch.object(periodic, "verify", side_effect=reject_first), \
                patch.object(periodic, "_compute_candidate_solution", side_effect=candidate_after_retry):
            result, pattern = periodic._solve_ultimately_periodic_initial(self.brs3)
        self.assertEqual(attempts, [0, 10])
        actual_sequence = periodic.utils.flatten_seq(pattern)
        _, expected_sequence = self.brs3.get_first_n_values(len(actual_sequence))
        self.assertEqual(actual_sequence, expected_sequence)
        self.assert_brs3_solution(result)

    def test_verified_prefix_can_extend_past_sampled_suffix(self):
        pattern = [([0, 1], 2), ([2, 3, 4], 1)]
        prefix = periodic._index_seq_prefix(pattern, 12)
        self.assertEqual(periodic.utils.flatten_seq(prefix),
                         [0, 1, 0, 1, 2, 3, 4, 2, 3, 4, 2, 3])

    def test_verified_candidate_at_last_attempt_is_accepted(self):
        result = periodic.solve_ultimately_periodic_initial(self.brs3, bnd=20)
        self.assert_brs3_solution(result)

    def assert_brs3_solution(self, result):
        n = self.brs3.ind_var
        solver = z3.Solver()
        expected = {"i": n+1, "s": 3*(n/3)}
        solver.add(n >= 0, z3.Or(*[
            expr != expected[str(app.decl().name())]
            for app, expr in result.as_dict().items()]))
        self.assertEqual(solver.check(), z3.unsat)

    def test_symbolic_constraints_use_full_prefix_lengths(self):
        pattern = [([0, 0], 2), ([1, 2, 2], 3)]
        candidate = self.candidate(self.prefix, pattern)
        constraint, variables = periodic._set_up_constraints(self.prefix, candidate, pattern)
        solver = z3.Solver()
        solver.add(*[k >= 0 for k in variables], z3.Not(constraint))
        self.assertEqual(solver.check(), z3.unsat)


if __name__ == "__main__":
    unittest.main()
