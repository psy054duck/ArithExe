from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))
from rec_solver.rec_parser import parse_str
from rec_solver.core.solver import solve_str
from rec_solver.core.tupling import TuplingUnsupported
from rec_solver.core.dag_reconstruction import solve_nearly_tail


class PointwiseSummaryTests(unittest.TestCase):
    def solve_through_scalarization(self, text):
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   wraps=solve_nearly_tail) as scalar:
            result = solve_str(text, multivariate_strategy="tupling").as_dict()
            scalar.assert_called_once()
            return result

    def equivalent(self, lhs, rhs):
        proof = z3.Solver()
        proof.set(timeout=5000)
        proof.add(lhs != rhs)
        self.assertEqual(proof.check(), z3.unsat)

    def test_polynomial_initialization_scan(self):
        text = '''if (Or(n<=0, k<0)) { A(k,n)=initial(k); }
                  else if (k==n-1) { A(k,n)=(k-1)*(k+1); }
                  else { A(k,n)=A(k,n-1); }'''
        rec = parse_str(text)
        k, n = rec.func_sig.children()
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        self.equivalent(self.solve_through_scalarization(text)[rec.func_sig],
                        z3.If(z3.And(k>=0, n>k), (k-1)*(k+1), initial(k)))

    def test_same_cell_increment(self):
        text = '''if (Or(n<=0, k<0)) { A(k,n)=initial(k); }
                  else if (k==n-1) { A(k,n)=A(k,n-1)+2; }
                  else { A(k,n)=A(k,n-1); }'''
        rec = parse_str(text)
        k, n = rec.func_sig.children()
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        self.equivalent(self.solve_through_scalarization(text)[rec.func_sig],
                        initial(k) + z3.If(z3.And(k>=0, n>k), 2, 0))

    def test_one_based_scan(self):
        text = '''if (Or(n<=0, k<1)) { A(k,n)=initial(k); }
                  else if (k==n) { A(k,n)=A(k,n-1)+3; }
                  else { A(k,n)=A(k,n-1); }'''
        rec = parse_str(text)
        k, n = rec.func_sig.children()
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        self.equivalent(self.solve_through_scalarization(text)[rec.func_sig],
                        initial(k) + z3.If(z3.And(k>=1, n>=k), 3, 0))

    def test_affine_entry_reads_after_dependence_substitution(self):
        for comparison in ("<", ">"):
            with self.subTest(comparison=comparison):
                text = f'''if (Or(n<=0, k<0)) {{ A(k,n)=initial(k); }}
                    else if (And(initial(n-1){comparison}bound, k==n-1)) {{
                        A(k,n)=initial(n-1);
                    }} else if (k==n-1) {{ A(k,n)=bound; }}
                    else {{ A(k,n)=A(k,n-1); }}'''
                rec = parse_str(text)
                k, n = rec.func_sig.children()
                initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
                bound = z3.Int("bound")
                condition = initial(k)<bound if comparison == "<" else initial(k)>bound
                expected = z3.If(z3.And(k>=0, n>k),
                                 z3.If(condition, initial(k), bound), initial(k))
                self.equivalent(self.solve_through_scalarization(text)[rec.func_sig], expected)

    def test_loop_invariant_parity(self):
        text = '''if (Or(n<=0, k<0)) { A(k,n)=initial(k); }
                  else if (And(k==n-1, bound%2==0)) { A(k,n)=A(k,n-1)+2; }
                  else if (k==n-1) { A(k,n)=A(k,n-1)+1; }
                  else { A(k,n)=A(k,n-1); }'''
        rec = parse_str(text)
        k, n = rec.func_sig.children()
        N = z3.Int("bound")
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        self.equivalent(self.solve_through_scalarization(text)[rec.func_sig],
                        initial(k) + z3.If(z3.And(k>=0, n>k), z3.If(N%2==0,2,1),0))

    def test_repeated_updates_are_solved_without_a_single_write_assumption(self):
        text = '''if (n<=0) { A(k,n)=initial(k); }
                  else { A(k,n)=A(k,n-1)+1; }'''
        rec = parse_str(text)
        k, n = rec.func_sig.children()
        initial = z3.Function("initial", z3.IntSort(), z3.IntSort())
        self.equivalent(self.solve_through_scalarization(text)[rec.func_sig],
                        initial(k) + z3.If(n > 0, n, 0))

    def test_cross_index_dependence_uses_the_existing_solver(self):
        text = '''if (Or(n<=0, k<0)) { A(k,n)=initial(k); }
                  else if (k==n-1) { A(k,n)=A(k-1,n-1)+1; }
                  else { A(k,n)=A(k,n-1); }'''
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   side_effect=RuntimeError("general scalar solver reached")):
            with self.assertRaisesRegex(RuntimeError, "general scalar solver reached"):
                solve_str(text, multivariate_strategy="tupling")

    def test_unknown_certification_does_not_accept_the_candidate(self):
        text = '''if (Or(n<=0, k<0)) { A(k,n)=initial(k); }
                  else if (k==n-1) { A(k,n)=1; }
                  else { A(k,n)=A(k,n-1); }'''
        with patch("rec_solver.core.tupling.certify_closed_form",
                   side_effect=TuplingUnsupported("unknown")):
            with self.assertRaises(TuplingUnsupported):
                solve_str(text, multivariate_strategy="tupling")


if __name__ == "__main__":
    unittest.main()
