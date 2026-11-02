from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))
sys.path.insert(0, str(ROOT / "tools"))
from rec_solver.rec_parser import parse_str
from rec_solver.core.tupling import (
    _Tupler, TuplingOptions, TuplingUnsupported, solve_tupled_rec,
)
from rec_solver.core.utils import conversion_cache
from rec_solver.core.prefix_scan import try_prefix_closed_form as prefix_candidate


class PrefixCandidateTests(unittest.TestCase):
    def recurrence(self, seed, update):
        return parse_str(f'''if (Or(n<=0,k<0)) {{ A(k,n)=initial(k); }}
            else if (And(n==1,k==0)) {{ A(k,n)={seed}; }}
            else if (And(n>=2,k==n-1)) {{ A(k,n)=A(k-1,n-1)+({update}); }}
            else {{ A(k,n)=A(k,n-1); }}''')

    def checker(self, rec):
        result = _Tupler(rec, TuplingOptions())
        result.ranking([(case.condition, tuple(next(iter(case.recursive_calls.values())).children()))
                        for case in rec.get_rec_cases()])
        return result

    def check_polynomial(self, seed, update, expected):
        rec = self.recurrence(seed, update)
        k, n = rec.func_sig.children()
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        with conversion_cache():
            candidate = prefix_candidate(self.checker(rec))
        self.assertIsNotNone(candidate)
        proof = z3.Solver()
        proof.set(timeout=5000)
        proof.add(candidate != z3.If(z3.And(k>=0,n>k), expected(k),initial(k)))
        self.assertEqual(proof.check(),z3.unsat)

    def test_constant_increment(self):
        self.check_polynomial("2", "1", lambda k:k+2)

    def test_quadratic_prefix(self):
        self.check_polynomial("0", "k+1", lambda k:k*(k+3)/2)

    def test_comp_first_pass(self):
        self.check_polynomial("0", "6", lambda k:6*k)

    def test_comp_second_pass(self):
        self.check_polynomial("1", "6*(k-1)", lambda k:3*k*k-3*k+1)

    def test_comp_third_pass(self):
        self.check_polynomial("0", "3*(k-1)*(k-1)-3*(k-1)+1",
                              lambda k:k*k*k-3*k*k+3*k)

    def test_quartic_result(self):
        self.check_polynomial("0", "4*k*k*k-6*k*k+4*k-1",lambda k:k*k*k*k)

    def test_symbolic_seed(self):
        self.check_polynomial("seed", "2",lambda k:z3.Int("seed")+2*k)

    def test_other_write_schedule_is_not_assumed(self):
        rec = parse_str('''if (Or(n<=0,k<0)) { A(k,n)=initial(k); }
            else if (k==n-2) { A(k,n)=A(k-1,n-1)+1; }
            else { A(k,n)=A(k,n-1); }''')
        self.assertIsNone(prefix_candidate(self.checker(rec)))

    def test_nonadjacent_read_is_not_a_prefix_scan(self):
        rec = parse_str('''if (Or(n<=0,k<0)) { A(k,n)=initial(k); }
            else if (And(n==1,k==0)) { A(k,n)=2; }
            else if (k==n-1) { A(k,n)=A(k-2,n-1)+1; }
            else { A(k,n)=A(k,n-1); }''')
        self.assertIsNone(prefix_candidate(self.checker(rec)))

    def test_incorrect_scalar_result_is_rejected(self):
        rec = self.recurrence("0", "1")
        t=z3.Int("ari_prefix_t")
        g=z3.Function("ari_prefix_value",z3.IntSort(),z3.IntSort())
        class Incorrect:
            def as_dict(self): return {g(t):z3.IntVal(999)}
        with patch("rec_solver.core.prefix_scan.solve_ultimately_periodic_symbolic",return_value=Incorrect()):
            with self.assertRaises(TuplingUnsupported):
                prefix_candidate(self.checker(rec))

    def test_unknown_certificate_cannot_accept_a_result(self):
        rec=self.recurrence("0","1")
        with patch("rec_solver.core.prefix_scan.certify_closed_form",side_effect=TuplingUnsupported("unknown")):
            with self.assertRaises(TuplingUnsupported):
                prefix_candidate(self.checker(rec))

    def test_all_original_base_cases_must_be_satisfied(self):
        rec=parse_str('''if (n<=0) { A(k,n)=n; }
            else if (k==0) { A(k,n)=0; }
            else { A(k,n)=A(k-1,n-1)+1; }''')
        with self.assertRaises(TuplingUnsupported):
            prefix_candidate(self.checker(rec))

    def test_integrated_dispatch_no_longer_uses_guessed_diagonal_summary(self):
        rec = self.recurrence("0", "k+1")
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   side_effect=RuntimeError("general solver reached")):
            with self.assertRaisesRegex(RuntimeError, "general solver reached"):
                solve_tupled_rec(rec)

    def test_failed_optional_candidate_uses_certified_general_result(self):
        rec = self.recurrence("2", "1")
        k, n = rec.func_sig.children()
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        expected = z3.If(z3.And(k >= 0, n > k), k+2, initial(k))
        with patch("rec_solver.core.prefix_scan.try_prefix_closed_form",
                   side_effect=TuplingUnsupported("unknown")):
            with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                       return_value=[expected]) as general:
                with conversion_cache():
                    closed = solve_tupled_rec(rec).as_dict()[rec.func_sig]
                general.assert_called_once()
        proof = z3.Solver()
        proof.set(timeout=5000)
        proof.add(closed != expected)
        self.assertEqual(proof.check(), z3.unsat)


if __name__ == "__main__":
    unittest.main()
