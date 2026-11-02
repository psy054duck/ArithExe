from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'rec_solver'))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core import solver as scalar_solver
from rec_solver.core import ultimately_periodic as periodic
from rec_solver.core.recurrence import LoopRecurrence
from rec_solver.core.solvable_polynomial import _parameter_integer_divisions
from rec_solver.core.verification import is_inductive_scalar_closed_form


class TransientCompositionTests(unittest.TestCase):
    def equivalent(self, actual, expected, *assumptions):
        check = z3.Solver()
        check.set(timeout=5000)
        check.add(*assumptions, actual != expected)
        self.assertEqual(check.check(), z3.unsat)

    def test_reset_then_increment_is_composed_chronologically(self):
        rec = parse_str('''
i(0)=0; s(0)=u;
if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=0; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+30; }
''')
        composed = LoopRecurrence.build_nonconditional_from_rec_by_seq(rec, [0, 1], {})
        n = rec.ind_var
        s = z3.Function('s', z3.IntSort(), z3.IntSort())
        self.equivalent(composed.transitions[0][z3.simplify(s(n+1))], z3.IntVal(30))

    def test_cross_variable_updates_remain_simultaneous(self):
        rec = parse_str('''
x(0)=p; y(0)=q;
if (x(n)==0) { x(n+1)=y(n); y(n+1)=x(n)+1; }
else { x(n+1)=x(n)+y(n); y(n+1)=2*y(n); }
''')
        composed = LoopRecurrence.build_nonconditional_from_rec_by_seq(rec, [0, 1], {})
        n = rec.ind_var
        x = z3.Function('x', z3.IntSort(), z3.IntSort())
        y = z3.Function('y', z3.IntSort(), z3.IntSort())
        self.equivalent(composed.transitions[0][z3.simplify(x(n+1))], y(n)+x(n)+1)
        self.equivalent(composed.transitions[0][z3.simplify(y(n+1))], 2*(x(n)+1))

    def test_reset_with_nonzero_periodic_increments_has_complete_solution(self):
        source = '''
i(0)=0; s(0)=u;
if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=0; }
else if (i(n)%3==0) { i(n+1)=i(n)+1; s(n+1)=s(n)+3; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+30; }
'''
        with patch.object(scalar_solver, 'poly_expr_solving',
                          side_effect=AssertionError('unexpected polynomial fallback')):
            result = solve_str(source)
        self.assertTrue(is_inductive_scalar_closed_form(
            parse_str(source), result, timeout_ms=5000, require_complete=True))
        n, u = z3.Ints('n u')
        expected = z3.If(n == 0, u, 30*(n-1)-27*((n-1)/3))
        self.assertEqual(len(result.as_dict()), 2)
        for app, expression in result.as_dict().items():
            self.equivalent(expression, n if app.decl().name() == 'i' else expected, n >= 0)

    def test_signed_parameter_division_covers_negative_and_positive_regions(self):
        source = '''
i(0)=0; s(0)=u;
if (And(i(n)==0,limit_value>=0)) { i(n+1)=i(n)+1; s(n+1)=10+limit_value/2; }
else if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=10-((-limit_value)/2); }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+1; }
'''
        with patch.object(scalar_solver, 'poly_expr_solving',
                          side_effect=AssertionError('unexpected polynomial fallback')):
            result = solve_str(source)
        self.assertTrue(is_inductive_scalar_closed_form(
            parse_str(source), result, timeout_ms=5000, require_complete=True))
        n, u, limit = z3.Ints('n u limit_value')
        expected = z3.If(n == 0, u, 9+n+z3.If(limit>=0, limit/2, -((-limit)/2)))
        self.assertEqual(len(result.as_dict()), 2)
        for app, expression in result.as_dict().items():
            self.equivalent(expression, n if app.decl().name() == 'i' else expected, n >= 0)

    def test_symbolic_template_keeps_reset_prefix_end_state(self):
        rec = parse_str('''
i(0)=0; s(0)=u;
if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=0; }
else if (i(n)%3==0) { i(n+1)=i(n)+1; s(n+1)=s(n)+3; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+30; }
''')
        candidate = periodic._compute_solution_by_index_seq(rec, [([0, 2], 1), ([2, 1, 2], 6)])
        self.assertTrue(is_inductive_scalar_closed_form(
            rec, candidate, timeout_ms=5000, require_complete=True))

    def test_quotient_parameters_do_not_capture_existing_inputs(self):
        source = '''
i(0)=0; s(0)=u;
if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=limit_value/2+_integer_quotient_0; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+1; }
'''
        with patch.object(scalar_solver, 'poly_expr_solving',
                          side_effect=AssertionError('unexpected polynomial fallback')):
            result = solve_str(source)
        self.assertTrue(is_inductive_scalar_closed_form(
            parse_str(source), result, timeout_ms=5000, require_complete=True))
        n, u, limit, other = z3.Ints('n u limit_value _integer_quotient_0')
        for app, expression in result.as_dict().items():
            expected = n if app.decl().name() == 'i' else z3.If(n==0, u, limit/2+other+n-1)
            self.equivalent(expression, expected, n>=0)

    def test_state_and_iteration_dependent_quotients_are_not_frozen(self):
        rec = parse_str('i(0)=0; if (true) { i(n+1)=i(n)/2+n/3; }')
        self.assertEqual(_parameter_integer_divisions(rec), [])

    def test_numeric_quotients_remain_numeric_coefficients(self):
        source = 'i(0)=0; s(0)=u; if (true) { i(n+1)=i(n)+1; s(n+1)=s(n)*(1/2); }'
        rec = parse_str(source)
        self.assertEqual(_parameter_integer_divisions(rec), [])
        with patch.object(scalar_solver, 'poly_expr_solving',
                          side_effect=AssertionError('unexpected polynomial fallback')):
            result = solve_str(source)
        self.assertTrue(is_inductive_scalar_closed_form(
            rec, result, timeout_ms=5000, require_complete=True))

    def test_quotient_parameter_avoids_iteration_and_function_names(self):
        for iteration_name, function_name in (('_integer_quotient_0', 's'),
                                               ('n', '_integer_quotient_0')):
            n = z3.Int(iteration_name)
            func = z3.Function(function_name, z3.IntSort(), z3.IntSort())
            transition = {func(n+1): z3.Int('limit_value')/2}
            rec = LoopRecurrence({func(0): z3.Int('u')},
                                 [(func(n)==0, transition), (z3.BoolVal(True), transition)])
            protected = _parameter_integer_divisions(rec)
            self.assertEqual(len(protected), 1)
            name = str(protected[0][1])
            self.assertNotEqual(name, str(rec.ind_var))
            self.assertNotIn(name, {decl.name() for decl in rec.func_decls})

    def test_composed_recurrence_retains_reverse_metadata(self):
        rec = parse_str('s(0)=u; if (true) { s(n+1)=s(n)+1; }')
        reverse = {next(iter(rec.func_decls))(rec.ind_var+1)}
        rec.set_reverse(reverse)
        composed = LoopRecurrence.build_nonconditional_from_rec_by_seq(rec, [0], {})
        self.assertEqual(composed.reverses, reverse)
        self.assertTrue(callable(composed.set_reverse))


if __name__ == '__main__':
    unittest.main()
