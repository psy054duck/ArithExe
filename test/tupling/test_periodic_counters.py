from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'rec_solver'))
from rec_solver.core.recurrence import LoopRecurrence
from rec_solver.core.periodic_counters import solve_periodic_counter
from rec_solver.core.verification import is_inductive_scalar_closed_form


class PeriodicCounters(unittest.TestCase):
    def recurrence(self, period, lower=501, upper=None, increment=1, residue=0):
        t, cap = z3.Ints('t cap')
        clock = z3.Function('clock', z3.IntSort(), z3.IntSort())
        count = z3.Function('count', z3.IntSort(), z3.IntSort())
        event = z3.And(clock(t) >= lower, clock(t) % period == residue, count(t) < cap)
        if upper is not None:
            event = z3.And(event, clock(t) < upper)
        rec = LoopRecurrence.mk_rec({clock(0): z3.IntVal(0), count(0): z3.IntVal(0)},
            [event, z3.Not(event)],
            [{clock(t+1): clock(t)+1, count(t+1): count(t)+increment},
             {clock(t+1): clock(t)+1, count(t+1): count(t)}])
        return rec, count, cap

    def check(self, period, lower, upper, residue=0):
        rec, count, cap = self.recurrence(period, lower, upper, residue=residue)
        result = solve_periodic_counter(rec)
        self.assertIsNotNone(result)
        self.assertTrue(is_inductive_scalar_closed_form(rec, result,
            timeout_ms=1000, require_complete=True))
        t = rec.ind_var
        end = t if upper is None else z3.If(t < upper, t, upper)
        raw = (end+period-1-residue)/period - (lower+period-1-residue)//period
        expected = z3.If(cap <= 0, 0, z3.If(raw <= 0, 0, z3.If(raw < cap, raw, cap)))
        solver = z3.Solver()
        solver.add(t >= 0, result.as_dict()[count(t)] != expected)
        self.assertEqual(solver.check(), z3.unsat)

    def test_delayed_saturation_periods_two_through_five(self):
        for period in range(2, 6):
            with self.subTest(period=period):
                self.check(period, 501, None)

    def test_finite_windows_and_nonpositive_caps(self):
        for period in range(2, 6):
            with self.subTest(period=period):
                self.check(period, 501, 1000)

    def test_shifted_residue(self):
        self.check(3, 7, None, residue=2)

    def test_zero_start_and_unit_period(self):
        self.check(1, 0, None)

    def test_changed_increment_is_not_recognized(self):
        rec, _, _ = self.recurrence(3, increment=2)
        self.assertIsNone(solve_periodic_counter(rec))

    def test_rejected_certificates_cannot_return_a_solution(self):
        rec, _, _ = self.recurrence(3)
        with patch('rec_solver.core.periodic_counters.is_inductive_scalar_closed_form',
                   return_value=False):
            self.assertIsNone(solve_periodic_counter(rec))

    def test_exhausted_budget_falls_back(self):
        rec, _, _ = self.recurrence(3)
        self.assertIsNone(solve_periodic_counter(rec, budget_ms=0))


if __name__ == '__main__':
    unittest.main()
