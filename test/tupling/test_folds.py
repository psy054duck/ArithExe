import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import z3

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "rec_solver"))
from rec_solver.core.aggregates import AggregateContext, solve_aggregate_recurrence, certify_aggregate_closed_form
from rec_solver.core.fold_operators import certify_operator, load_operators
from rec_solver.rec_parser import parse_str


MAX = {"name": "max", "sort": "Int", "combine": "(ite (>= a b) a b)",
       "laws": ["associative", "commutative", "idempotent"]}


class GeneralFolds(unittest.TestCase):
    def solve_with_max(self, text):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'operators.json'
            config.write_text(json.dumps([MAX]))
            with patch.dict(os.environ, {'ARITHEXE_FOLD_OPERATORS': str(config)}):
                rec = parse_str(text)
                return rec, solve_aggregate_recurrence(rec)

    def test_piecewise_update_body_is_discovered_and_certified(self):
        rec, closed = self.solve_with_max('''m(0)=seed;
            if(And(n<2,a(2*n)>m(n))){m(n+1)=a(2*n);}
            else if(And(n>=2,n<5,b(n-1)>m(n))){m(n+1)=b(n-1);}
            else if(And(n>=5,c(n)>m(n))){m(n+1)=c(n);}
            else{m(n+1)=m(n);}''')
        self.assertIsNotNone(closed)
        self.assertTrue(certify_aggregate_closed_form(rec, closed))
        self.assertEqual([d['kind'] for d in closed.aggregates.export()], ['fold'])
        a, b, c = [z3.Function(name, z3.IntSort(), z3.IntSort()) for name in ('a','b','c')]
        expected = z3.Int('seed')
        value = next(iter(closed.as_dict().values()))
        for n in range(9):
            actual = closed.aggregates.expand(z3.substitute(value, (rec.ind_var,z3.IntVal(n))))
            self.equivalent(actual, expected)
            body = a(2*n) if n<2 else b(n-1) if n<5 else c(n)
            expected = z3.If(expected>=body,expected,body)

    def test_piecewise_body_cannot_weaken_original_accumulator_guards(self):
        for update in ('a(n)+1', 'a(n)-2'):
            _, closed = self.solve_with_max(f'''m(0)=seed;
                if(And(n<2,a(n)>m(n))){{m(n+1)={update};}}
                else if(And(n>=2,b(n)>m(n))){{m(n+1)=b(n);}}
                else{{m(n+1)=m(n);}}''')
            self.assertIsNone(closed)

    def test_projection_does_not_certify_state_selected_inputs(self):
        _, closed = self.solve_with_max('''m(0)=seed;
            if(m(n)<0){m(n+1)=a(n);}
            else{m(n+1)=b(n);}''')
        self.assertIsNone(closed)

    def equivalent(self, actual, expected):
        solver = z3.Solver()
        solver.add(actual != expected)
        self.assertEqual(solver.check(), z3.unsat)

    def test_laws_and_false_identity(self):
        op = certify_operator(MAX, z3.main_ctx())
        self.assertEqual(op.name, "max")
        with self.assertRaises(ValueError):
            certify_operator(dict(MAX, identity=0), z3.main_ctx())
        with self.assertRaises(ValueError):
            certify_operator(dict(MAX, combine="(- a b)"), z3.main_ctx())

    def test_reject_external_and_quantified_operations(self):
        for text in ["c", "(ite (forall ((i Int)) (> i a)) a b)"]:
            with self.assertRaises((ValueError, z3.Z3Exception)):
                certify_operator(dict(MAX, combine=text), z3.main_ctx())

    def test_ordered_nonassociative_fold_and_empty_seed(self):
        op = certify_operator(dict(MAX, combine="(- a b)", laws=[]), z3.main_ctx())
        context = AggregateContext()
        i, n = z3.Ints("i n")
        a = z3.Function("entry", z3.IntSort(), z3.IntSort())
        fold = context.make_fold(op, i, 0, n, z3.IntVal(7), a(i))
        for count in range(5):
            actual = context.expand(z3.substitute(fold, (n, z3.IntVal(count))))
            self.equivalent(actual, 7 - sum((a(k) for k in range(count)), z3.IntVal(0)))

    def test_max_recurrence_and_export(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "operators.json"
            config.write_text(json.dumps([MAX]))
            with patch.dict(os.environ, {"ARITHEXE_FOLD_OPERATORS": str(config)}):
                rec = parse_str("m(0)=v;if(a(n)>m(n)){m(n+1)=a(n);}else{m(n+1)=m(n);}")
                closed = solve_aggregate_recurrence(rec)
                self.assertIsNotNone(closed)
                self.assertTrue(certify_aggregate_closed_form(rec, closed))
                value = next(iter(closed.as_dict().values()))
                expected = z3.Int("v")
                a = z3.Function("a", z3.IntSort(), z3.IntSort())
                for count in range(5):
                    actual = closed.aggregates.expand(z3.substitute(value, (rec.ind_var, z3.IntVal(count))))
                    self.equivalent(actual, expected)
                    expected = z3.If(expected >= a(count), expected, a(count))
                exported = closed.aggregates.export()
                self.assertEqual(exported[0]["kind"], "fold")
                self.assertEqual(len(z3.parse_smt2_string(exported[0]["combine_smt2"])), 3)

    def test_unknown_law_is_rejected(self):
        with self.assertRaises(ValueError):
            certify_operator(dict(MAX, laws=["magical"]), z3.main_ctx())

    def test_inconclusive_law_is_not_trusted(self):
        with patch("rec_solver.core.fold_operators.z3.Solver.check", return_value=z3.unknown):
            with self.assertRaises(ValueError):
                certify_operator(MAX, z3.main_ctx())

    def test_minimum_uses_the_supplied_seed(self):
        op = certify_operator(dict(MAX, name="min", combine="(ite (<= a b) a b)"), z3.main_ctx())
        context = AggregateContext()
        i = z3.Int("index")
        value = context.make_fold(op, i, 0, 3, z3.IntVal(8), 5-i)
        self.equivalent(value, z3.IntVal(3))
        empty = context.make_fold(op, i, 4, 2, z3.IntVal(8), 5-i)
        self.equivalent(empty, z3.IntVal(8))


if __name__ == "__main__":
    unittest.main()
