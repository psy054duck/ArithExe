from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'rec_solver'))
from rec_solver.core.polynomial_sums import polynomial_sum
from rec_solver.core.aggregates import solve_aggregate_recurrence, certify_aggregate_closed_form
from rec_solver.rec_parser import parse_str
from rec_solver import solve_str


class PolynomialSums(unittest.TestCase):
    def setUp(self):
        self.i,self.r,self.s,self.lo,self.hi = z3.Ints('i r s lo hi')

    def check_concrete(self,body):
        value = polynomial_sum(self.i,self.lo,self.hi,body)
        self.assertIsNotNone(value)
        for lower,upper in ((-4,-1),(-2,4),(0,0),(3,1),(0,6)):
            for r in (-5,-1,0,2,8):
                for s in (-2,1):
                    params = ((self.lo,z3.IntVal(lower)),(self.hi,z3.IntVal(upper)),
                              (self.r,z3.IntVal(r)),(self.s,z3.IntVal(s)))
                    actual = z3.simplify(z3.substitute(value,*params))
                    expected = sum(z3.simplify(z3.substitute(body,*params,
                        (self.i,z3.IntVal(i)))).as_long() for i in range(lower,upper))
                    self.assertEqual(actual.as_long(),expected,(lower,upper,r,s,body))

    def test_affine_comparisons_and_negative_coefficients(self):
        i,r = self.i,self.r
        for condition in (i<r,i<=r,i>r,i>=r,i==r,i!=r,-i+r>=1):
            self.check_concrete(z3.If(condition,i*i+r,2*i-r))

    def test_boolean_partition_is_disjoint(self):
        i,r,s = self.i,self.r,self.s
        for condition in (z3.And(i<r,i>=s),z3.Or(i<r,i>=s),
                          z3.Not(z3.And(i==r,i<s)),
                          z3.Or(i==r,i<=r,i<s)):
            self.check_concrete(z3.If(condition,i+r,s-i))

    def test_nested_piecewise_polynomials(self):
        i,r,s = self.i,self.r,self.s
        self.check_concrete(i*z3.If(i<r,r-i,z3.If(i==s,0,i-r))+s)

    def test_invariant_boolean_and_nondefault_context(self):
        ctx = z3.Context()
        i,n = z3.Ints('i n',ctx=ctx)
        flag = z3.Bool('flag',ctx=ctx)
        value = polynomial_sum(i,z3.IntVal(0,ctx),n,z3.If(flag,i,-i))
        self.assertIsNotNone(value)
        for choice in (True,False):
            actual = z3.simplify(z3.substitute(value,(flag,z3.BoolVal(choice,ctx)),
                                              (n,z3.IntVal(5,ctx))))
            self.assertEqual(actual.as_long(),10 if choice else -10)

    def test_integer_valued_quotient_and_high_degree(self):
        i,r = self.i,self.r
        self.check_concrete(i*(i-1)/2+r*i)
        self.check_concrete(i*i*i*i)

    def test_nonexact_division_and_unsupported_guards_fall_back(self):
        i,r = self.i,self.r
        for body in (i/2,z3.If(i*i<r,i,0),z3.If(i%2==0,i,0),
                     z3.If(2*i<r,i,0), i*i*i*i*i):
            self.assertIsNone(polynomial_sum(i,self.lo,self.hi,body))

    def test_unknown_inputs_are_not_polynomial_coefficients(self):
        a = z3.Function('entry',z3.IntSort(),z3.IntSort())
        self.assertIsNone(polynomial_sum(self.i,self.lo,self.hi,a(self.i)))

    def test_budget_exhaustion_is_not_partial_sum(self):
        with patch('rec_solver.core.polynomial_sums.time.monotonic',side_effect=(0,2)):
            self.assertIsNone(polynomial_sum(self.i,self.lo,self.hi,self.i))

    def test_early_path_and_certified_seeded_triangular_recurrence(self):
        text = '''i(0)=0;s(0)=seed;
            if(i(n)==r){i(n+1)=i(n)+1;s(n+1)=s(n);}
            else{i(n+1)=i(n)+1;s(n+1)=s(n)+i(n);}'''
        rec = parse_str(text)
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        self.assertTrue(certify_aggregate_closed_form(rec,closed))
        self.assertFalse(closed.aggregates.definitions)
        with patch('rec_solver.core.solver.solve_ultimately_periodic_symbolic',
                   side_effect=AssertionError('slow solver called')):
            self.assertIsNotNone(solve_str(text,multivariate_strategy='tupling'))
        values = closed.as_dict()
        tampered = type(closed)({lhs:rhs+1 for lhs,rhs in values.items()},rec.ind_var,
                               closed.aggregates,closed.operators)
        self.assertFalse(certify_aggregate_closed_form(rec,tampered))

    def test_residual_pure_arithmetic_defers_to_existing_solver(self):
        for text in (
            'x(0)=1;if(n<r){x(n+1)=2*x(n);}else{x(n+1)=3*x(n);}',
            'i(0)=0;x(0)=seed;if(i(n)==0){i(n+1)=i(n)+1;x(n+1)=0;}'
            'else{i(n+1)=i(n)+1;x(n+1)=x(n)+1;}',
        ):
            aggregate = solve_aggregate_recurrence(parse_str(text))
            self.assertIsNotNone(aggregate)
            self.assertTrue(aggregate.aggregates.definitions)
            sentinel = object()
            with patch('rec_solver.core.solver.solve_ultimately_periodic_symbolic',
                       return_value=sentinel) as periodic:
                self.assertIs(solve_str(text,multivariate_strategy='tupling'),sentinel)
                periodic.assert_called_once()

    def test_external_inputs_retain_the_existing_early_aggregate_path(self):
        text = 'x(0)=0;if(true){x(n+1)=x(n)+entry(n);}'
        with patch('rec_solver.core.solver.solve_ultimately_periodic_symbolic',
                   side_effect=AssertionError('external input fell through')):
            result = solve_str(text,multivariate_strategy='tupling')
        self.assertTrue(result.aggregates.definitions)


if __name__ == '__main__':
    unittest.main()
