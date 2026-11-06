import itertools
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver import parse_str, solve_str
from rec_solver.core.tupling import (
    _Tupler, TuplingOptions, TuplingUnsupported, tuple_recurrence,
    certify_closed_form,
)
from rec_solver.core.tuple_polynomials import solve_polynomial_array_tuple, _synthesize
from rec_solver.core.phase_lengths import PhasePlan
from rec_solver.core.ultimately_periodic import UnsolvableError, _solve_phase_template
from test_tupling import evaluate, fingerprint


class GuardedTupleArrays(unittest.TestCase):
    def setUp(self):
        self.text = (Path(__file__).parent / "multipath_array_progression.rec").read_text()
        self.rec = parse_str(self.text)

    def test_frontier_tuple_preserves_all_components_and_source(self):
        before = fingerprint(self.rec)
        result = tuple_recurrence(self.rec)
        self.assertFalse(self.rec.is_nearly_tail())
        self.assertTrue(result.recurrence.is_nearly_tail())
        self.assertGreater(len(result.components), 1)
        self.assertEqual(before, fingerprint(self.rec))
        original, tupled = {}, {}
        for values in itertools.product(range(-2, 9), range(-1, 8)):
            actual = evaluate(result.recurrence, values, tupled)
            substitution = list(zip(self.rec.func_sig.children(), map(z3.IntVal, values)))
            for index, component in enumerate(result.components):
                arguments = [z3.simplify(z3.substitute(v, substitution)).as_long()
                             for v in component]
                expected = evaluate(self.rec, arguments, original)[0]
                self.assertTrue(z3.is_true(z3.simplify(actual[index] == expected)),
                                (values, index, actual[index], expected))

    def test_guarded_multicall_reaches_certified_polynomial_scalar_solution(self):
        with patch("rec_solver.core.tupling.tuple_recurrence", wraps=tuple_recurrence) as tupler:
            with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                       side_effect=AssertionError("slow APS fallback used")):
                result = solve_str(self.text, multivariate_strategy="tupling")
        tupler.assert_called_once()
        q, t = self.rec.func_sig.children()
        entry = z3.Function("entry", z3.IntSort(), z3.IntSort())
        expected = z3.If(q == 0, 0, z3.If(q == 1, 1,
            z3.If(z3.And(q >= 2, t >= q-1), q, entry(q))))
        proof = z3.Solver()
        proof.set(timeout=1000)
        proof.add(result.as_dict()[self.rec.func_sig] != expected)
        self.assertEqual(proof.check(), z3.unsat)

    def test_wrong_seed_cannot_certify_the_expected_progression(self):
        changed = parse_str(self.text.replace("A(q,t)=1;", "A(q,t)=2;"))
        q, t = changed.func_sig.children()
        entry = z3.Function("entry", z3.IntSort(), z3.IntSort())
        value = z3.If(q == 0, 0, z3.If(q == 1, 1,
            z3.If(z3.And(q >= 2, t >= q-1), q, entry(q))))
        with self.assertRaisesRegex(TuplingUnsupported, "original recurrence"):
            certify_closed_form(changed, value)

    def test_alternating_writes_have_distinct_certified_values(self):
        text = (self.text.replace("A(q,t)=1;", "A(q,t)=2;")
                .replace("2*A(t,t-1)-A(t-1,t-1);", "2*A(t,t-1)-A(t-1,t-1)-2;")
                .replace("3*A(t,t-1)-2*A(t-1,t-1)-1;", "3*A(t,t-1)-2*A(t-1,t-1)+2;"))
        rec = parse_str(text)
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   side_effect=AssertionError("slow APS fallback used")):
            solution = solve_str(text, multivariate_strategy="tupling")
        q, t = rec.func_sig.children()
        entry = z3.Function("entry", z3.IntSort(), z3.IntSort())
        expected = z3.If(q == 0, 0, z3.If(q == 1, 2,
            z3.If(z3.And(q >= 2, t >= q-1), q + q % 2, entry(q))))
        proof = z3.Solver()
        proof.set(timeout=1000)
        proof.add(solution.as_dict()[rec.func_sig] != expected)
        self.assertEqual(proof.check(), z3.unsat)

    def test_phase_template_rejects_residual_quantifiers(self):
        n, k = z3.Ints("phase_n phase_k")
        predicate = z3.Function("phase_predicate", z3.IntSort(), z3.BoolSort())
        rec = SimpleNamespace(ind_var=n)
        budget = SimpleNamespace(stats={"qe_calls": 0, "qe_seconds": 0.0})
        module = "rec_solver.core.ultimately_periodic"
        with patch(f"{module}._compute_solution_by_index_seq"), \
             patch(f"{module}._set_up_constraints", return_value=(predicate(k), [k])):
            with self.assertRaisesRegex(UnsolvableError, "remains quantified"):
                _solve_phase_template(rec, [([0], 1)], PhasePlan((), ()),
                                      z3.BoolVal(True), True, budget)

    def test_guard_equality_extension_is_certified(self):
        checker = _Tupler(self.rec, TuplingOptions())
        q, t = checker.args
        values = (t, t-1)
        candidates = list(checker.anchor_candidates(q == t+1, values))
        expected = (q-1, t-1)
        self.assertTrue(any(all(z3.eq(z3.simplify(a), z3.simplify(b))
                               for a, b in zip(candidate, expected))
                            for candidate in candidates))
        self.assertTrue(checker.equivalent(q == t+1, values, expected))
        self.assertFalse(checker.equivalent(z3.BoolVal(True), values, expected))

    def test_unconstrained_entry_seeds_do_not_produce_a_polynomial_claim(self):
        text = self.text.replace("A(q,t)=0;", "A(q,t)=entry(q);")
        text = text.replace("A(q,t)=1;", "A(q,t)=entry(q);")
        rec = parse_str(text)
        result = tuple_recurrence(rec)
        self.assertIsNone(solve_polynomial_array_tuple(_Tupler(rec, TuplingOptions()), result))

    def test_synthesis_unknown_and_expired_budget_are_not_candidates(self):
        import time
        coefficient, n = z3.Ints("coefficient n")
        obligations = [(n >= 0, coefficient*n == n)]
        self.assertIsNone(_synthesize(obligations, [coefficient], 0))
        with patch("z3.Solver.check", return_value=z3.unknown):
            self.assertIsNone(_synthesize(obligations, [coefficient], time.monotonic()+1))

    def test_rare_branch_cannot_be_missed_by_sampling(self):
        import time
        coefficient, n = z3.Ints("rare_coefficient rare_n")
        obligations = [(n != 1000, coefficient*n == n),
                       (n == 1000, coefficient*n == n+1)]
        self.assertIsNone(_synthesize(obligations, [coefficient], time.monotonic()+1))


if __name__ == "__main__":
    unittest.main()
