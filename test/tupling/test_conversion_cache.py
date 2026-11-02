from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))
sys.path.insert(0, str(ROOT))
import solver as worker_solver
from rec_solver.core import utils
from rec_solver.core.solver import solve_str


class ConversionCacheTests(unittest.TestCase):
    def setUp(self):
        self.x = z3.Int("conversion_x")

    def counted(self):
        return patch.object(utils, "_to_sympy_uncached", wraps=utils._to_sympy_uncached)

    def test_reuses_immutable_conversions_within_a_request(self):
        with self.counted() as convert, utils.conversion_cache():
            first = utils.to_sympy(self.x + 1)
            self.assertIs(first, utils.to_sympy(self.x + 1))
        self.assertEqual(convert.call_count, 1)

    def test_requests_do_not_share_entries(self):
        with self.counted() as convert:
            for _ in range(2):
                with utils.conversion_cache():
                    utils.to_sympy(self.x)
        self.assertEqual(convert.call_count, 2)

    def test_cache_is_disabled_outside_a_request(self):
        with self.counted() as convert:
            utils.to_sympy(self.x)
            utils.to_sympy(self.x)
        self.assertEqual(convert.call_count, 2)

    def test_integer_assumptions_are_part_of_the_key(self):
        with utils.conversion_cache():
            integer = utils.to_sympy(self.x)
            generic = utils.to_sympy(self.x, is_integer=False)
            self.assertTrue(integer.is_integer)
            self.assertIsNone(generic.is_integer)

    def test_different_contexts_do_not_share_entries(self):
        left, right = z3.Context(), z3.Context()
        with self.counted() as convert, utils.conversion_cache():
            utils.to_sympy(z3.Int("same", ctx=left))
            utils.to_sympy(z3.Int("same", ctx=right))
        self.assertEqual(convert.call_count, 2)

    def test_cache_is_bounded_and_uses_lru_eviction(self):
        with self.counted() as convert, utils.conversion_cache(maxsize=2):
            for value in [self.x, self.x+1, self.x, self.x+2, self.x+1]:
                utils.to_sympy(value)
        self.assertEqual(convert.call_count, 4)

    def test_nested_solves_share_the_request_scope(self):
        with self.counted() as convert, utils.conversion_cache():
            utils.to_sympy(self.x)
            with utils.conversion_cache():
                utils.to_sympy(self.x)
            utils.to_sympy(self.x)
        self.assertEqual(convert.call_count, 1)

    def test_exception_restores_the_request_scope(self):
        with self.assertRaisesRegex(RuntimeError, "request failed"):
            with utils.conversion_cache():
                utils.to_sympy(self.x)
                raise RuntimeError("request failed")
        self.assertIsNone(utils._conversion_cache.get())

    def test_failed_conversions_are_not_cached(self):
        with patch.object(utils, "_to_sympy_uncached", side_effect=ValueError("unsupported")) as convert:
            with utils.conversion_cache():
                for _ in range(2):
                    with self.assertRaises(ValueError):
                        utils.to_sympy(self.x)
        self.assertEqual(convert.call_count, 2)

    def test_negative_division_and_boolean_expressions_match_uncached_conversion(self):
        expressions = [self.x / 2, (-self.x) / 3, self.x % 2,
                       z3.And(self.x >= 0, self.x < 4), z3.BoolVal(True)]
        with utils.conversion_cache():
            for expression in expressions:
                self.assertEqual(utils.to_sympy(expression),
                                 utils._to_sympy_uncached(expression))

    def test_solver_restores_scope_after_failure(self):
        with self.assertRaises(ValueError):
            solve_str("", multivariate_strategy="unsupported")
        self.assertIsNone(utils._conversion_cache.get())

    def test_worker_export_shares_the_request_cache(self):
        def export(closed):
            self.assertIsNotNone(utils._conversion_cache.get())
            return "exported"
        with patch.object(worker_solver, "solve_str", return_value=object()):
            with patch.object(worker_solver, "closed_form_to_smt2", side_effect=export):
                self.assertEqual(worker_solver.solve_str_to_smt2(""), "exported")
        self.assertIsNone(utils._conversion_cache.get())

    def test_rejects_nonpositive_capacity(self):
        with self.assertRaises(ValueError), utils.conversion_cache(maxsize=0):
            pass


if __name__ == "__main__":
    unittest.main()
