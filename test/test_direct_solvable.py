"""Regressions for canonical indices and direct polynomial summation."""
import unittest
from unittest.mock import patch
import os
import z3
from solver import solve_str_to_smt2
from rec_solver.rec_parser import parse_str
from rec_solver.core.solvable_polynomial import is_solvable_map
from rec_solver.core.closed_form import ExprClosedForm
from rec_solver.core.solver import _direct_function_closed_form_is_inductive

class DirectSolvable(unittest.TestCase):
    def test_fourth_power_sum_uses_direct_route_with_symbolic_initials(self):
        for index in ('n+1', '1+n'):
            text = f'y(0)=y0; x(0)=x0; if(true){{y({index})=y(n)+1; x({index})=x(n)+(y(n)+1)*(y(n)+1)*(y(n)+1)*(y(n)+1);}}'
            rec = parse_str(text)
            self.assertTrue(is_solvable_map(rec))
            with patch('rec_solver.core.solver.solve_bounded_cfinite_map', side_effect=AssertionError('fallback used')), patch(
                    'rec_solver.core.solver.solve_ultimately_periodic_symbolic', side_effect=AssertionError('sampling used')):
                result = z3.parse_smt2_string(solve_str_to_smt2(text))
            x, y, n, x0, y0 = z3.Ints('x y n x0 y0')
            check = z3.Solver()
            check.set(timeout=3000)
            check.add(result, x0==0, y0==0, n==4, z3.Or(x!=354, y!=4))
            self.assertEqual(check.check(), z3.unsat)

    def test_fallback_unknown_rejects_candidate_and_has_timeout(self):
        rec = parse_str('x(0)=0; if(true){x(n+1)=x(n)+1;}')
        n = rec.ind_var
        closed = ExprClosedForm({next(iter(rec.func_decls))(n):n}, n)
        with patch.dict(os.environ, {'ARITHEXE_RECURRENCE_CERT_TIMEOUT_MS':'25'}), patch('rec_solver.core.solver.z3.Solver') as factory:
            factory.return_value.check.return_value = z3.unknown
            self.assertFalse(_direct_function_closed_form_is_inductive(rec, closed))
            factory.return_value.set.assert_called_once_with(timeout=25)

if __name__ == '__main__':
    unittest.main()
