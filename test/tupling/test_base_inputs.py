from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core.base_inputs import _affine, normalize_base_inputs
from rec_solver.core import utils
from rec_solver.core.dag_reconstruction import solve_nearly_tail
from rec_solver.core.tupling import TuplingOptions, TuplingUnsupported, certify_closed_form


class BaseInputTests(unittest.TestCase):
    def equivalent(self, actual, expected, domain=z3.BoolVal(True)):
        solver = z3.Solver()
        solver.set(timeout=5000)
        solver.add(domain, actual != expected)
        self.assertEqual(solver.check(), z3.unsat)

    def solve(self, text):
        rec = parse_str(text)
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail", wraps=solve_nearly_tail) as backend:
            result = solve_str(text, multivariate_strategy="tupling").as_dict()[rec.func_sig]
            backend.assert_called_once()
        return rec, result

    def test_shifted_affine_base_is_evaluated_at_terminal_arguments(self):
        rec, result = self.solve('''if(t<=0){f(k,t)=input(2*k+1);}
            else{f(k,t)=f(k-1,t-1)+1;}''')
        k, t = rec.func_sig.children()
        oracle = z3.Function("input", z3.IntSort(), z3.IntSort())
        self.equivalent(result, oracle(2*(k-t)+1)+t, t>=0)

    def test_multidimensional_external_base_arguments(self):
        rec, result = self.solve('''if(t<=0){f(k,t)=input(k+2*t,3*k-t);}
            else{f(k,t)=f(k,t-1)+2;}''')
        k, t = rec.func_sig.children()
        oracle = z3.Function("input", z3.IntSort(), z3.IntSort(), z3.IntSort())
        self.equivalent(result, oracle(k,3*k)+2*t, t>=0)

    def test_multiple_input_arrays_in_base_value(self):
        rec, result = self.solve('''if(t<=0){f(k,t)=left(k+1)+2*right(3*k);}
            else{f(k,t)=f(k,t-1)+1;}''')
        k, t = rec.func_sig.children()
        left = z3.Function("left", z3.IntSort(), z3.IntSort())
        right = z3.Function("right", z3.IntSort(), z3.IntSort())
        self.equivalent(result, left(k+1)+2*right(3*k)+t, t>=0)

    def test_signed_constant_coefficients_are_normalized_before_validation(self):
        k, t = z3.Ints('k t')
        one, two = z3.IntVal(1), z3.IntVal(2)
        for expression in (-one*k, -(one+two)*(t-one),
                           -one+two*k+(-one)*(t-one), (one-two-two)*(k+t)):
            with self.subTest(expression=expression):
                self.assertTrue(_affine(expression))

    def test_negative_coefficients_do_not_hide_variable_products(self):
        k, t = z3.Ints('k t')
        for expression in ((-k)*t, (-z3.IntVal(1))*k*t, (k+1)*(t-1)):
            with self.subTest(expression=expression):
                self.assertFalse(_affine(expression))

    def test_signed_affine_base_is_evaluated_at_terminal_arguments(self):
        rec, result = self.solve('''if(t<=0){f(k,t)=input((-1)*k+(-2)*(t+1));}
            else{f(k,t)=f(k-1,t-1)+1;}''')
        k, t = rec.func_sig.children()
        oracle = z3.Function('input', z3.IntSort(), z3.IntSort())
        self.equivalent(result, oracle(-(k-t)-2)+t, t>=0)

    def test_two_ended_copy_with_parsed_negative_coefficients(self):
        rec, result = self.solve('''if(Or(t<=0,k<0)){copy(k,t)=initial(k);}
            else if(k==t-1){copy(k,t)=entry(t-1);}
            else if(k==(-1)+2*size+(-1)*(t-1)){
                copy(k,t)=entry((-1)+2*size+(-1)*(t-1));
            }else{copy(k,t)=copy(k,t-1);}''')
        k, t = rec.func_sig.children()
        size = z3.Int('size')
        entry = z3.Function('entry', z3.IntSort(), z3.IntSort())
        initial = z3.Function('initial', z3.IntSort(), z3.IntSort())
        written = z3.Or(z3.And(0<=k, k<t), z3.And(2*size-t<=k, k<2*size))
        self.equivalent(result, z3.If(written, entry(k), initial(k)),
                        z3.And(size>1, 0<=t, t<=size))
        completed = z3.substitute(result, (t, size))
        self.equivalent(completed, entry(k), z3.And(size>1, 0<=k, k<2*size))

    def test_varying_input_in_guard_is_rejected_not_frozen(self):
        text = '''if(t<=0){f(k,t)=input(k);}
            else if(input(t)>0){f(k,t)=f(k,t-1)+1;}
            else{f(k,t)=f(k,t-1);}'''
        with self.assertRaisesRegex(TuplingUnsupported, "input application varies"):
            solve_str(text, multivariate_strategy="tupling")

    def test_equality_gated_oracle_and_complementary_frame_are_normalized(self):
        rec, result = self.solve('''if(Or(t<=0,k<0)){f(k,t)=input(k);}
            else if(And(k==t,oracle(t-1)>0)){f(k,t)=k+j+oracle(t-1);}
            else{f(k,t)=f(k,t-1);}''')
        k, t = rec.func_sig.children()
        oracle = z3.Function('oracle', z3.IntSort(), z3.IntSort())
        initial = z3.Function('input', z3.IntSort(), z3.IntSort())
        expected = z3.If(z3.And(k>=1, k<=t, oracle(k-1)>0),
                         k+z3.Int('j')+oracle(k-1), initial(k))
        self.equivalent(result, expected, t>=0)

    def test_equality_inside_disjunction_cannot_rewrite_unmasked_oracle(self):
        text = '''if(t<=0){f(k,t)=input(k);}
            else if(Or(k==t,oracle(t)>0)){f(k,t)=f(k,t-1)+1;}
            else{f(k,t)=f(k,t-1);}'''
        with self.assertRaisesRegex(TuplingUnsupported, 'input application varies'):
            solve_str(text, multivariate_strategy='tupling')

    def test_input_parameters_round_trip_through_scalar_algebra_without_collisions(self):
        rec = parse_str('''if(t<=0){f(k,t)=input(k);}
            else if(input(k)>0){f(k,t)=f(k,t-1)+ari_entry_input_0;}
            else{f(k,t)=f(k,t-1);}''')
        _, restoration = normalize_base_inputs(rec, TuplingOptions())
        self.assertEqual(len(restoration), 1)
        parameter = restoration[0][0]
        self.assertNotEqual(parameter.decl().name(), 'ari_entry_input_0')
        self.assertTrue(z3.eq(utils.to_z3(utils.to_sympy(parameter)), parameter))

    def test_nonlinear_external_argument_is_not_accepted_as_affine(self):
        rec = parse_str('''if(t<=0){f(k,t)=input(k*k);}
            else{f(k,t)=f(k,t-1)+1;}''')
        with self.assertRaisesRegex(ValueError, "affine"):
            normalize_base_inputs(rec, TuplingOptions())

    def test_unknown_invariance_check_cannot_freeze_a_guard(self):
        rec = parse_str('''if(t<=0){f(k,t)=input(k);}
            else if(input(k)>0){f(k,t)=f(k,t-1)+1;}
            else{f(k,t)=f(k,t-1);}''')
        with patch("rec_solver.core.base_inputs._InputContext.proves", return_value=False):
            with self.assertRaises(TuplingUnsupported):
                normalize_base_inputs(rec, TuplingOptions())

    def guard_only_recurrence(self):
        return parse_str('''if(t<=0){f(k,t)=0;}
            else if(oracle(k)>0){f(k,t)=f(k,t-1)+1;}
            else{f(k,t)=f(k,t-1);}''')

    def test_guard_only_input_survives_solving_and_certification(self):
        rec, result = self.solve('''if(t<=0){f(k,t)=0;}
            else if(oracle(k)>0){f(k,t)=f(k,t-1)+1;}
            else{f(k,t)=f(k,t-1);}''')
        k, t = rec.func_sig.children()
        oracle = z3.Function('oracle', z3.IntSort(), z3.IntSort())
        self.equivalent(result, z3.If(z3.And(t>0, oracle(k)>0), t, 0))

    def test_guard_only_input_does_not_allow_fabricated_functions(self):
        rec = self.guard_only_recurrence()
        k, t = rec.func_sig.children()
        invented = z3.Function('invented', z3.IntSort(), z3.IntSort())
        with self.assertRaisesRegex(TuplingUnsupported, 'unresolved function calls'):
            certify_closed_form(rec, z3.If(t>0, invented(k), 0))

    def test_guard_only_input_does_not_allow_residual_recursive_calls(self):
        rec = self.guard_only_recurrence()
        k, t = rec.func_sig.children()
        with self.assertRaisesRegex(TuplingUnsupported, 'unresolved function calls'):
            certify_closed_form(rec, rec.func_sig.decl()(k, t-1))

    def test_guard_only_input_still_requires_original_equations(self):
        rec = self.guard_only_recurrence()
        k, t = rec.func_sig.children()
        oracle = z3.Function('oracle', z3.IntSort(), z3.IntSort())
        for value in (z3.IntVal(0),
                      z3.If(z3.And(t>0, oracle(k+1)>0), t, 0)):
            with self.subTest(candidate=value):
                with self.assertRaisesRegex(TuplingUnsupported, 'original recurrence equation'):
                    certify_closed_form(rec, value)

    def test_two_lane_guard_only_clamp(self):
        rec, result = self.solve('''if(Or(t<=0,k<0)){f(k,t)=initial(k);}
            else if(And(entry(2*t-2)>2*t-2,entry(2*t-1)<=2*t-1,k==2*t-2)){
                f(k,t)=2*t-2;
            }else if(And(entry(2*t-2)<=2*t-2,entry(2*t-1)>2*t-1,k==2*t-1)){
                f(k,t)=2*t-1;
            }else if(And(entry(2*t-2)>2*t-2,entry(2*t-1)>2*t-1,k==2*t-1)){
                f(k,t)=2*t-1;
            }else if(And(entry(2*t-2)>2*t-2,entry(2*t-1)>2*t-1,k!=2*t-1,k==2*t-2)){
                f(k,t)=2*t-2;
            }else{f(k,t)=f(k,t-1);}''')
        k, t = rec.func_sig.children()
        entry = z3.Function('entry', z3.IntSort(), z3.IntSort())
        initial = z3.Function('initial', z3.IntSort(), z3.IntSort())
        expected = z3.If(z3.And(0<=k, k<2*t, entry(k)>k), k, initial(k))
        self.equivalent(result, expected, t>=0)


if __name__ == "__main__":
    unittest.main()
