from pathlib import Path
import json
import sys
import unittest
from unittest.mock import patch

import sympy as sp
import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
import solver
from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core.aggregates import (
    AggregateContext, certify_aggregate_closed_form, solve_aggregate_recurrence,
)
from rec_solver.core import utils


class FiniteAggregates(unittest.TestCase):
    def test_overwrite_recurrence_preserves_seed_without_zero_products(self):
        rec = parse_str('i(0)=0;x(0)=seed;if(true){i(n+1)=i(n)+1;x(n+1)=p+i(n);}')
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        self.assertTrue(certify_aggregate_closed_form(rec,closed))
        self.assertFalse(closed.aggregates.definitions)
        value = closed.as_dict()[z3.Function('x',z3.IntSort(),z3.IntSort())(rec.ind_var)]
        for n in range(7):
            actual = z3.substitute(value,(rec.ind_var,z3.IntVal(n)))
            self.equivalent(actual,z3.Int('seed') if n==0 else z3.Int('p')+n-1)

    def test_constant_body_export_retains_signature_on_left(self):
        aggregates = AggregateContext(preserve_symbolic=True)
        i,n = z3.Ints('export_i export_n')
        value = aggregates.make('product',i,0,n,z3.IntVal(0))
        self.assertIn(value.decl(),aggregates.definitions)
        metadata, = aggregates.export()
        binding = z3.parse_smt2_string(metadata['smt2'])[0]
        self.assertEqual(binding.arg(0).decl().name(),value.decl().name())
        self.assertTrue(z3.is_int_value(binding.arg(1)))
        self.equivalent(aggregates.expand(z3.substitute(value,(n,z3.IntVal(0)))),z3.IntVal(1))
        self.equivalent(aggregates.expand(z3.substitute(value,(n,z3.IntVal(3)))),z3.IntVal(0))

    def test_preserved_small_sums_expand_only_when_requested(self):
        i = z3.Int('preserved_i')
        a = z3.Function('preserved_a', z3.IntSort(), z3.IntSort())
        for n in (15, 16, 17, 20, 31, 32, 33, 40):
            aggregates = AggregateContext(preserve_symbolic=True)
            term = aggregates.make('sum', i, 0, n, a(i))
            self.assertIn(term.decl(), aggregates.definitions)
            if n <= 32:
                self.assertNotIn(aggregates.expand(term).decl(), aggregates.definitions)
        eager = AggregateContext()
        self.assertNotIn(eager.make('sum', i, 0, 2, a(i)).decl(), eager.definitions)

    def equivalent(self, lhs, rhs):
        check = z3.Solver()
        check.add(lhs != rhs)
        self.assertEqual(check.check(), z3.unsat)

    def test_equivalent_piecewise_binders_are_certified_before_reuse(self):
        aggregates = AggregateContext()
        i, j, n, m = z3.Ints('i j n m')
        a = z3.Function('entry', z3.IntSort(), z3.IntSort())
        first = aggregates.make('sum', i, 0, n, z3.If(i < n, i+a(i), a(0)))
        second = aggregates.make('sum', j, 0, m, z3.If(j <= m-1, j+a(j), a(0)))
        self.assertEqual(first.decl(), second.decl())
        self.assertEqual(len(aggregates.definitions), 1)
        changed = aggregates.make('sum', j, 0, m, z3.If(j <= m-1, j+1+a(j), a(0)))
        self.assertNotEqual(first.decl(), changed.decl())

    def test_inconclusive_binder_comparison_does_not_reuse_a_definition(self):
        aggregates = AggregateContext()
        i, j, n = z3.Ints('i j n')
        a = z3.Function('entry', z3.IntSort(), z3.IntSort())
        first = aggregates.make('sum', i, 0, n, z3.If(i < n, a(i), a(0)))
        with patch('rec_solver.core.aggregates._prove', return_value=False):
            second = aggregates.make('sum', j, 0, n, z3.If(j <= n-1, a(j), a(0)))
        self.assertNotEqual(first.decl(), second.decl())

    def test_sum_and_shifted_counter(self):
        rec = parse_str('i(0)=h; s(0)=v; if(true){i(n+1)=i(n)+1; s(n+1)=s(n)+a(i(n));}')
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        self.assertTrue(certify_aggregate_closed_form(rec, closed))
        t = rec.ind_var
        s = z3.Function('s', z3.IntSort(), z3.IntSort())
        a = z3.Function('a', z3.IntSort(), z3.IntSort())
        h, v = z3.Ints('h v')
        for n in (0, 1, 5):
            actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[s(t)], (t, z3.IntVal(n))))
            self.equivalent(actual, v + sum((a(h + k) for k in range(n)), z3.IntVal(0)))

    def test_product_keeps_zero_factors_and_empty_product(self):
        rec = parse_str('p(0)=v; if(true){p(n+1)=p(n)*a(n);}')
        closed = solve_aggregate_recurrence(rec)
        t, v = rec.ind_var, z3.Int('v')
        p, a = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('p', 'a')]
        expected = v
        for n in range(5):
            actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[p(t)], (t, z3.IntVal(n))))
            self.equivalent(actual, expected)
            expected = expected * a(n)
        value = closed.aggregates.expand(z3.substitute(closed.as_dict()[p(t)], (t, z3.IntVal(3))))
        self.equivalent(z3.substitute(value, (a(1), z3.IntVal(0))), z3.IntVal(0))

    def test_parser_distinguishes_constant_and_multidimensional_input_accesses(self):
        rec = parse_str('i(0)=0;s(0)=0;if(true){i(n+1)=i(n)+1;s(n+1)=s(n)+a(0)+b(i(n),h);}')
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        t, h = rec.ind_var, z3.Int('h')
        s, a = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('s', 'a')]
        b = z3.Function('b', z3.IntSort(), z3.IntSort(), z3.IntSort())
        actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[s(t)], (t, z3.IntVal(2))))
        self.equivalent(actual, 2*a(0)+b(0,h)+b(1,h))

    def test_general_affine_formula_matches_direct_execution(self):
        rec = parse_str('x(0)=v; if(true){x(n+1)=a(n)*x(n)+b(n);}')
        closed = solve_aggregate_recurrence(rec)
        self.assertTrue(certify_aggregate_closed_form(rec, closed))
        t = rec.ind_var
        x, a, b = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('x', 'a', 'b')]
        expected = z3.Int('v')
        for n in range(5):
            actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[x(t)], (t, z3.IntVal(n))))
            self.equivalent(actual, expected)
            expected = a(n) * expected + b(n)

    def test_conditional_count_with_entry_array_guard(self):
        rec = parse_str('s(0)=0; if(a(n)>0){s(n+1)=s(n)+1;}else{s(n+1)=s(n);}')
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        t = rec.ind_var
        s, a = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('s', 'a')]
        actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[s(t)], (t, z3.IntVal(3))))
        self.equivalent(actual, sum((z3.If(a(i)>0, 1, 0) for i in range(3))))

    def test_triangular_system_with_nested_sums(self):
        rec = parse_str('x(0)=0;y(0)=0;if(true){x(n+1)=x(n)+a(n);y(n+1)=y(n)+x(n);}')
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        t = rec.ind_var
        y, a = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('y', 'a')]
        actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[y(t)], (t, z3.IntVal(3))))
        self.equivalent(actual, 2*a(0)+a(1))

    def test_counter_updates_merge_across_piecewise_input_values(self):
        rec = parse_str('''i(0)=0;s(0)=0;
if(i(n)==0){i(n+1)=i(n)+1;s(n+1)=s(n)+a(1);}
else if(i(n)==1){i(n+1)=i(n)+1;s(n+1)=s(n)+a(0);}
else{i(n+1)=i(n)+1;s(n+1)=s(n)+a(i(n));}''')
        closed = solve_aggregate_recurrence(rec)
        self.assertIsNotNone(closed)
        t = rec.ind_var
        s, a = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('s', 'a')]
        actual = closed.aggregates.expand(z3.substitute(closed.as_dict()[s(t)], (t, z3.IntVal(5))))
        self.equivalent(actual, sum(a(i) for i in range(5)))

    def test_feedback_and_nonlinear_updates_are_rejected(self):
        for text in [
            'x(0)=1;if(true){x(n+1)=x(n)*x(n)+a(n);}',
            'x(0)=0;if(x(n)>0){x(n+1)=x(n)+a(n);}else{x(n+1)=x(n)-1;}',
            'x(0)=0;y(0)=0;if(true){x(n+1)=y(n)+a(n);y(n+1)=x(n);}',
            'x(0)=0;if(true){x(n+1)=a(x(n));}',
        ]:
            with self.subTest(text=text):
                self.assertIsNone(solve_aggregate_recurrence(parse_str(text)))

    def test_tampered_candidate_fails_certificate(self):
        rec = parse_str('s(0)=0;if(true){s(n+1)=s(n)+a(n);}')
        closed = solve_aggregate_recurrence(rec)
        key = next(iter(closed.as_dict()))
        closed._closed_forms[key] += 1
        self.assertFalse(certify_aggregate_closed_form(rec, closed))

    def test_unknown_smt_certificate_is_not_accepted(self):
        rec = parse_str('s(0)=0;if(true){s(n+1)=s(n)+a(n);}')
        with patch('rec_solver.core.aggregates._prove', return_value=False):
            self.assertIsNone(solve_aggregate_recurrence(rec))

    def test_alpha_renaming_and_explicit_parameters(self):
        ctx = AggregateContext()
        i, j, n, h = z3.Ints('i j n h')
        a = z3.Function('a', z3.IntSort(), z3.IntSort())
        first = ctx.make('sum', i, 0, n, a(i+h))
        second = ctx.make('sum', j, 0, n, a(j+h))
        self.assertTrue(z3.eq(first, second))
        value = ctx.expand(z3.substitute(first, (h, z3.IntVal(4)), (n, z3.IntVal(2))))
        self.equivalent(value, a(4)+a(5))

    def test_zero_and_large_ranges_do_not_unroll(self):
        ctx = AggregateContext()
        i, n = z3.Ints('i n')
        a = z3.Function('a', z3.IntSort(), z3.IntSort())
        value = ctx.make('sum', i, 0, n, a(i))
        self.equivalent(ctx.expand(z3.substitute(value, (n, z3.IntVal(0)))), z3.IntVal(0))
        large = ctx.expand(z3.substitute(value, (n, z3.IntVal(100000))))
        self.assertIn(large.decl(), ctx.definitions)

    def test_worker_response_carries_definitions_separately(self):
        value = solve_str('s(0)=0;if(true){s(n+1)=s(n)+a(n);}')
        response = json.loads(solver.closed_form_to_smt2(value))
        self.assertEqual(response['format'], 'arith-exe-aggregates-v1')
        summary = z3.parse_smt2_string(response['summary'])
        self.assertEqual(len(summary), 1)
        self.assertTrue(z3.is_eq(summary[0]))
        self.assertEqual(response['aggregates'][0]['kind'], 'sum')
        self.assertEqual(len(z3.parse_smt2_string(response['aggregates'][0]['smt2'])), 2)

    def test_legacy_plain_sum_placeholder_is_rejected(self):
        i, n = sp.symbols('i n', integer=True)
        a = sp.Function('a')
        for expression in (sp.Sum(a(i), (i, 0, n)), sp.Product(a(i), (i, 0, n))):
            with self.assertRaisesRegex(ValueError, 'certified aggregate context'):
                utils.to_z3(expression)


if __name__ == '__main__':
    unittest.main()
