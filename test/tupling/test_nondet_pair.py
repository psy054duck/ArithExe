from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'rec_solver'))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core.base_inputs import normalize_base_inputs
from rec_solver.core.tupling import TuplingOptions, TuplingUnsupported
from rec_solver.core import utils


def paired_array_text():
    text = 'if(Or(t<=0,k<0)){a(k,t)=initial(k);}'
    for first in (True, False):
        for second in (True, False):
            left = 'read(2*t-2)'+('!=0' if first else '==0')
            right = 'read(2*t-1)'+('!=0' if second else '==0')
            for offset, nonzero in ((1, second), (2, first)):
                value = f'read(2*t-{offset})' if nonzero else '10'
                text += (f'else if(And({left},{right},k==2*t-{offset}))'
                         f'{{a(k,t)={value};}}')
    return text+'else{a(k,t)=a(k,t-1);}'


class NondetPairTests(unittest.TestCase):
    def equivalent(self, actual, expected, domain):
        solver = z3.Solver()
        solver.set(timeout=5000)
        solver.add(domain, actual != expected)
        self.assertEqual(solver.check(), z3.unsat)

    def test_exhaustive_equal_scalar_updates_avoid_polynomial_search(self):
        text = 'i(0)=1;c(0)=0;'
        for first in (True, False):
            for second in (True, False):
                left = 'read(c(n))'+('!=0' if first else '==0')
                right = 'read(c(n)+1)'+('!=0' if second else '==0')
                text += ('if' if first and second else 'else if')
                text += f'(And({left},{right})){{i(n+1)=i(n)+1;c(n+1)=c(n)+2;}}'
        rec = parse_str(text)
        self.assertEqual(len(rec.conditions), 1)
        self.assertTrue(z3.is_true(rec.conditions[0]))
        with patch('rec_solver.core.solver.poly_expr_solving',
                   side_effect=AssertionError('unexpected polynomial fallback')):
            result = solve_str(text, poly_expr_strategy='general', poly_expr_order=4).as_dict()
        n = rec.ind_var
        for lhs, value in result.items():
            expected = n+1 if lhs.decl().name() == 'i' else 2*n
            self.equivalent(value, expected, n>=0)

    def test_nonexhaustive_scalar_update_preserves_identity_branch(self):
        rec = parse_str('i(0)=0;if(read(n)>0){i(n+1)=i(n)+1;}')
        self.assertEqual(len(rec.conditions), 2)
        self.assertFalse(any(z3.is_true(c) for c in rec.conditions))

    def test_paired_accesses_normalize_to_queried_element(self):
        rec = parse_str(paired_array_text())
        normalized, restore = normalize_base_inputs(rec, TuplingOptions())
        k, t = rec.func_sig.children()
        for parameter, term in restore:
            self.assertNotIn(t, utils.get_vars(term))
        for case in normalized.get_base_cases()+normalized.get_rec_cases():
            for term in utils.get_applied_functions(case.condition):
                self.assertNotIn(t, utils.get_vars(term))

    def test_paired_array_closed_form_preserves_unwritten_elements(self):
        text = paired_array_text()
        rec = parse_str(text)
        result = solve_str(text, multivariate_strategy='tupling').as_dict()[rec.func_sig]
        k, t = rec.func_sig.children()
        read = z3.Function('read', z3.IntSort(), z3.IntSort())
        initial = z3.Function('initial', z3.IntSort(), z3.IntSort())
        expected = z3.If(z3.And(0<=k, k<2*t),
                         z3.If(read(k)!=0, read(k), 10), initial(k))
        self.equivalent(result, expected, t>=0)

    def test_neighbor_input_is_preserved_as_neighbor_not_same_element(self):
        text = '''if(t<=0){a(k,t)=initial(k);}
            else if(And(k==2*t-2,read(2*t-1)>0)){a(k,t)=read(2*t-2);}
            else{a(k,t)=a(k,t-1);}'''
        # Here read(2*t-1) can safely become read(k+1), not read(k).
        rec = parse_str(text)
        _, restoration = normalize_base_inputs(rec, TuplingOptions())
        k, _ = rec.func_sig.children()
        read = z3.Function('read', z3.IntSort(), z3.IntSort())
        self.assertTrue(any(z3.is_true(z3.simplify(term == read(k+1)))
                            for _, term in restoration))

    def test_uncertified_access_rewrite_is_rejected(self):
        rec = parse_str('''if(t<=0){a(k,t)=initial(k);}
            else if(Or(k==2*t-2,read(2*t-1)>0)){a(k,t)=a(k,t-1)+1;}
            else{a(k,t)=a(k,t-1);}''')
        with self.assertRaises(TuplingUnsupported):
            normalize_base_inputs(rec, TuplingOptions())


if __name__ == '__main__':
    unittest.main()
