from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver.rec_parser import parse_str
from rec_solver.core.base_inputs import normalize_base_inputs
from rec_solver.core.dag_reconstruction import compute_stopping_time, reconstruct_base_return, solve_nearly_tail
from rec_solver.core.multivariate import solve_nearly_tail as legacy_solve, symbol2func
from rec_solver.core.recurrence import BaseCase, MultiRecurrence
from rec_solver.core.tupling import TuplingOptions


class DagReconstructionTests(unittest.TestCase):
    def equivalent(self, actual, expected, assumption=z3.BoolVal(True)):
        solver = z3.Solver()
        solver.set(timeout=5000)
        solver.add(assumption, actual != expected)
        self.assertEqual(solver.check(), z3.unsat)

    def test_terminal_arguments_and_vector_accumulators_are_substituted_simultaneously(self):
        k, t, d = z3.Ints("k t _d")
        r0, r1 = z3.Ints("_ret0 _ret1")
        f = z3.Function("vector_f", z3.IntSort(), z3.IntSort(), z3.IntSort())
        u = z3.Function("input_u", z3.IntSort(), z3.IntSort())
        v = z3.Function("input_v", z3.IntSort(), z3.IntSort())
        rec = MultiRecurrence(f(k,t), [BaseCase(t<=0, (u(k),v(2*k)))], [])
        state = Mock()
        state.as_dict.return_value = {
            symbol2func(k)(d): k-d, symbol2func(t)(d): t-d,
            symbol2func(r0)(d): r0+2*r1+d,
            symbol2func(r1)(d): 3*r0-r1,
        }
        state.subs.side_effect = AssertionError("must not rebuild closed-form objects")
        actual = reconstruct_base_return(rec,d,[r0,r1],state,t)
        self.equivalent(actual[0],u(k-t)+2*v(2*(k-t))+t)
        self.equivalent(actual[1],3*u(k-t)-v(2*(k-t)))
        state.as_dict.assert_called_once()
        state.subs.assert_not_called()

    def test_piecewise_base_alternatives_stay_as_ites(self):
        k, p, d, ret = z3.Ints("k p _d _ret0")
        f = z3.Function("f",z3.IntSort(),z3.IntSort())
        u = z3.Function("input_u",z3.IntSort(),z3.IntSort())
        rec = MultiRecurrence(f(k), [BaseCase(z3.BoolVal(True), [
            ((u(k),), p>0), ((u(k)+1,), p<=0)])], [])
        state = Mock()
        state.as_dict.return_value = {symbol2func(k)(d):k, symbol2func(ret)(d):ret}
        with patch("rec_solver.core.multivariate.expr2piecewise", side_effect=AssertionError("expanded")), \
             patch("rec_solver.core.multivariate.merge_cases", side_effect=AssertionError("merged")):
            actual, = reconstruct_base_return(rec,d,[ret],state,z3.IntVal(0))
        self.equivalent(actual,z3.If(p>0,u(k),u(k)+1))

    def test_nested_terminal_condition_preserves_both_base_results(self):
        k, t, d, ret = z3.Ints("k t _d _ret0")
        flag = z3.Bool("choose_base")
        f = z3.Function("f",z3.IntSort(),z3.IntSort(),z3.IntSort())
        rec = MultiRecurrence(f(k,t), [BaseCase(t<=0,(k+1,)),BaseCase(t>0,(k+2,))], [])
        state = Mock()
        state.as_dict.return_value = {symbol2func(k)(d):k,
            symbol2func(t)(d):z3.If(flag,0,1),symbol2func(ret)(d):ret}
        actual, = reconstruct_base_return(rec,d,[ret],state,z3.IntVal(0))
        self.equivalent(actual,z3.If(flag,k+1,k+2))

    def test_many_base_cases_have_linear_size_shared_dag(self):
        k, mode, d, ret = z3.Ints("k mode _d _ret0")
        f = z3.Function("f",z3.IntSort(),z3.IntSort())
        u = z3.Function("input_u",z3.IntSort(),z3.IntSort())
        conditions = [mode==i for i in range(60)]
        cases = [BaseCase(condition,(u(k+i),)) for i,condition in enumerate(conditions)]
        cases.append(BaseCase(z3.Not(z3.Or(*conditions)),(u(k+60),)))
        rec = MultiRecurrence(f(k),cases,[])
        state = Mock()
        state.as_dict.return_value = {symbol2func(k)(d):k,symbol2func(ret)(d):ret}
        actual, = reconstruct_base_return(rec,d,[ret],state,z3.IntVal(0))
        seen, todo = set(), [actual]
        while todo:
            expr = todo.pop()
            if expr.get_id() in seen:
                continue
            seen.add(expr.get_id())
            todo.extend(expr.children())
        self.assertLess(len(seen),500)
        for i in (0,1,29,59):
            self.equivalent(actual,u(k+i),mode==i)
        self.equivalent(actual,u(k+60),mode<0)

    def test_matches_legacy_reconstruction_on_affine_input_base(self):
        rec = parse_str('''if(t<=0){f(k,t)=input(2*k+1);}
            else{f(k,t)=f(k-1,t-1)+1;}''')
        self.equivalent(solve_nearly_tail(rec)[0],legacy_solve(rec)[0])

    def test_integer_row_stopping_time_with_nonconstant_base_return(self):
        rec = parse_str('''if(Or(t<=0,k<0)){f(k,t)=input(k);}
            else if(And(5*(t-1)<=k,k<5*t)){f(k,t)=2*(t-1)+k;}
            else{f(k,t)=f(k,t-1);}''')
        k, t = rec.func_sig.children()
        entry = z3.Function('input', z3.IntSort(), z3.IntSort())
        expected = z3.If(z3.And(k >= 0, k < 5*t), 2*(k/5)+k, entry(k))
        self.equivalent(solve_nearly_tail(rec)[0], expected)

    def test_stopping_time_does_not_default_over_missing_regions(self):
        k, t, d, stop = z3.Ints('k t _d _D')
        state = Mock()
        closed = Mock()
        closed.as_dict.return_value = {symbol2func(t)(d):t-d}
        state._constraints = [k >= 0]
        state._closed_forms = [closed]
        with self.assertRaisesRegex(ValueError, 'cover'):
            compute_stopping_time(d, stop, symbol2func(t)(d) <= 0,
                                 state, z3.BoolVal(True))

    def test_matches_legacy_reconstruction_on_conditional_entry_reads(self):
        rec = parse_str('''if(Or(t<=0,k<0)){f(k,t)=input(k);}
            else if(And(k==t-1,input(t-1)<bound)){f(k,t)=input(t-1);}
            else if(k==t-1){f(k,t)=bound;}
            else{f(k,t)=f(k,t-1);}''')
        target, restore = normalize_base_inputs(rec,TuplingOptions())
        actual = z3.substitute(solve_nearly_tail(target)[0],restore)
        expected = z3.substitute(legacy_solve(target)[0],restore)
        self.equivalent(actual,expected)


if __name__ == "__main__":
    unittest.main()
