"""OOPSLA23 periodic sampling must never invoke whole-prefix compression."""
import os
import unittest
from unittest.mock import patch
import z3
from solver import solve_str_to_smt2
from rec_solver.core.paper_periodic import generate, symbolic_template


class PaperPeriodic(unittest.TestCase):
    def test_generator(self):
        self.assertEqual(generate([9, 0, 1, 0, 1]), (1, [0, 1]))
        self.assertEqual(generate([9, 1, 0, 1, 0, 1]), (1, [1, 0]))
        self.assertEqual(generate([0, 1, 2]), (0, []))
        self.assertEqual(generate([0] * 8), (0, [0]))

    def test_section71_has_only_one_unknown_count(self):
        template, qs = symbolic_template([([0], 3), ([1], 4), ([2], 1)])
        self.assertEqual(len(qs), 1)
        self.assertEqual(template[1], ([1, 1, 1, 1], 1))

    def check_formula(self, text, bad):
        with patch.dict(os.environ, {'ARITHEXE_PERIODIC_STRATEGY': 'oopsla23'}), patch(
                'rec_solver.core.utils.compress_seq', side_effect=AssertionError('compression used')):
            formula = z3.parse_smt2_string(solve_str_to_smt2(text))
        check = z3.Solver()
        check.set(timeout=5000)
        check.add(formula, bad)
        self.assertEqual(check.check(), z3.unsat)

    def test_first_violation_restart(self):
        n, x = z3.Ints('n x')
        self.check_formula('x(0)=0; if(x(n)<5){x(n+1)=x(n)+1;}else{x(n+1)=x(n)+2;}',
                           z3.And(n >= 0, x != z3.If(n < 5, n, 2*n-5)))

    def test_symbolic_initial_regions(self):
        n, x, x0 = z3.Ints('n x x0')
        self.check_formula('x(0)=x0; if(x(n)>0){x(n+1)=x(n)+1;}else{x(n+1)=x(n)-1;}',
                           z3.And(n >= 0, x != z3.If(x0 > 0, x0+n, x0-n)))

    def test_period_two(self):
        n, x = z3.Ints('n x')
        self.check_formula('x(0)=0; if(x(n)<1){x(n+1)=x(n)+1;}else{x(n+1)=x(n)-1;}',
                           z3.And(n >= 0, x != n % 2))

    def test_unknown_not_accepted(self):
        from rec_solver.core.ultimately_periodic import verify, UnsolvableError
        from rec_solver.rec_parser import parse_str
        from rec_solver.core.ultimately_periodic import _compute_solution_by_index_seq
        rec = parse_str('x(0)=0; if(x(n)<5){x(n+1)=x(n)+1;}else{x(n+1)=x(n)+2;}')
        candidate = _compute_solution_by_index_seq(rec, [([0], 1)])
        with patch('rec_solver.core.ultimately_periodic.z3.Solver') as factory:
            factory.return_value.check.return_value = z3.unknown
            with self.assertRaises(UnsolvableError):
                verify(rec, candidate, [([0], 1)])


if __name__ == '__main__':
    unittest.main()
