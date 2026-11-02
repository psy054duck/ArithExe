import itertools
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))
sys.path.insert(0, str(ROOT))

from rec_solver import parse_str, solve_str
from rec_solver.core.recurrence import BaseCase, MultiRecurrence, RecursiveCase
from rec_solver.core.tupling import (
    TuplingOptions, TuplingUnsupported, tuple_recurrence, certify_closed_form,
)


def evaluate(rec, values, memo=None):
    if memo is None:
        memo = {}
    key = tuple(values)
    if key in memo:
        return memo[key]
    args = rec.func_sig.children()
    substitution = [(a, z3.IntVal(v)) for a, v in zip(args, values)]
    for case in rec.get_base_cases() + rec.get_rec_cases():
        guard = z3.simplify(z3.substitute(case.condition, substitution))
        if not z3.is_true(guard):
            continue
        results = []
        if isinstance(case, RecursiveCase):
            distinct_calls = {}
            for index, (name, call) in enumerate(case.recursive_calls.items()):
                child = tuple(z3.simplify(z3.substitute(a, substitution)).as_long()
                              for a in call.children())
                if child not in distinct_calls:
                    distinct_calls[child] = evaluate(rec, child, memo)
                returned = distinct_calls[child]
                results.append((name, returned[index] if len(returned) > 1 else returned[0]))
        answer = tuple(z3.simplify(z3.substitute(op, substitution + results))
                       for op in case.op)
        memo[key] = answer
        return answer
    raise AssertionError(f"no branch for {values}")


def fingerprint(rec):
    return [(c.condition.sexpr(), tuple(o.sexpr() for o in c.op),
             tuple((n.sexpr(), f.sexpr()) for n, f in
                   getattr(c, "recursive_calls", {}).items()))
            for c in rec.get_base_cases() + rec.get_rec_cases()]


class TuplingTests(unittest.TestCase):
    def fixture(self, name):
        return parse_str((Path(__file__).parent / name).read_text())

    def assert_exact_tuple(self, rec, inputs):
        before = fingerprint(rec)
        result = tuple_recurrence(rec)
        self.assertEqual(before, fingerprint(rec))
        self.assertTrue(result.recurrence.is_nearly_tail())
        self.assertGreater(result.proof_obligations, 0)
        source_memo, tuple_memo = {}, {}
        for values in inputs:
            actual = evaluate(result.recurrence, values, tuple_memo)
            substitution = [(a, z3.IntVal(v)) for a, v in
                            zip(rec.func_sig.children(), values)]
            for index, component in enumerate(result.components):
                source_args = [z3.simplify(z3.substitute(a, substitution)).as_long()
                               for a in component]
                expected = evaluate(rec, source_args, source_memo)[0]
                self.assertTrue(z3.is_true(z3.simplify(actual[index] == expected)),
                                (values, index, actual[index], expected))
        return result

    def test_fibonacci(self):
        result = self.assert_exact_tuple(self.fixture("fibonacci.rec"),
                                         [(n,) for n in range(-4, 24)])
        self.assertEqual(len(result.components), 2)

    def test_guarded_cross_argument_updates(self):
        result = self.assert_exact_tuple(
            self.fixture("guarded_affine.rec"),
            itertools.product(range(-2, 13), range(-2, 13), range(-2, 3)))
        self.assertEqual(len(result.components), 2)

    def test_multidimensional_array_parameters(self):
        result = self.assert_exact_tuple(
            self.fixture("multidimensional.rec"),
            itertools.product(range(4), range(5), range(-2, 15)))
        self.assertEqual(len(result.components), 2)

    def test_multiple_recursive_branches(self):
        rec = parse_str("""if (n <= 1) { f(n,a) = a; }
            else if (n % 2 == 0) { f(n,a) = f(n-1,a) + f(n-2,a); }
            else { f(n,a) = 2*f(n-1,a) + f(n-2,a); }""")
        self.assert_exact_tuple(rec, itertools.product(range(-2, 18), range(-2, 3)))

    def test_three_call_recurrence(self):
        rec = parse_str("""if (n <= 2) { f(n,a)=a+n; }
            else { f(n,a)=2*f(n-1,a)+3*f(n-2,a)-f(n-3,a); }""")
        result = self.assert_exact_tuple(rec, itertools.product(range(-2, 16), range(-2, 3)))
        self.assertEqual(len(result.components), 3)

    def test_bad_input_type(self):
        with self.assertRaisesRegex(TuplingUnsupported, "MultiRecurrence"):
            tuple_recurrence(None)

    def test_existing_single_call_case(self):
        self.assert_exact_tuple(self.fixture("triangular.rec"),
                                itertools.product(range(-3, 20), range(-2, 3)))

    def test_end_to_end_scalar_closed_form(self):
        text = (Path(__file__).parent / "triangular.rec").read_text()
        rec = parse_str(text)
        closed = solve_str(text, multivariate_strategy="tupling")
        expression = closed.as_dict()[rec.func_sig]
        n, a = rec.func_sig.children()
        for nv, av in itertools.product(range(-4, 32), range(-3, 4)):
            actual = z3.simplify(z3.substitute(expression,
                                 (n, z3.IntVal(nv)), (a, z3.IntVal(av))))
            expected = av + (nv * (nv + 1) // 2 if nv > 0 else 0)
            self.assertEqual(actual.as_long(), expected)

    def test_nearly_tail_input_skips_tuple_construction(self):
        text = (Path(__file__).parent / "triangular.rec").read_text()
        with patch("rec_solver.core.tupling.tuple_recurrence") as tupler:
            closed = solve_str(text, multivariate_strategy="tupling")
            self.assertTrue(closed.as_dict())
            tupler.assert_not_called()

    def test_eqn1f_singular_call_reaches_scalar_solver_without_tupling(self):
        text = (Path(__file__).parent / "eqn1f_single_call.rec").read_text()
        rec = parse_str(text)
        self.assertTrue(rec.is_nearly_tail())
        i, n = rec.func_sig.children()
        initial = rec.get_base_cases()[0].op[0].decl()
        expected = z3.If(z3.And(i >= 1, n >= i), initial(0) + 20*i, initial(i))
        # A known exact candidate isolates dispatch from scalar-solver support.
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   return_value=[expected]) as scalar:
            with patch("rec_solver.core.tupling.tuple_recurrence") as tupler:
                with patch("rec_solver.core.tupling._Tupler.inverse") as inverse:
                    closed = solve_str(text, multivariate_strategy="tupling")
                    scalar.assert_called_once()
                    self.assertTrue(scalar.call_args.args[0].is_nearly_tail())
                    tupler.assert_not_called()
                    inverse.assert_not_called()
        self.assertTrue(z3.eq(closed.as_dict()[rec.func_sig], expected))

    def test_nearly_tail_singular_map_end_to_end(self):
        text = """if (n <= 0) { f(n,a) = a; }
            else { f(n,a) = f(n-1,0) + a + 1; }"""
        rec = parse_str(text)
        self.assertTrue(rec.is_nearly_tail())
        with patch("rec_solver.core.tupling.tuple_recurrence") as tupler:
            closed = solve_str(text, multivariate_strategy="tupling")
            tupler.assert_not_called()
        n, a = rec.func_sig.children()
        proof = z3.Solver()
        proof.add(closed.as_dict()[rec.func_sig] != a + z3.If(n > 0, n, 0))
        self.assertEqual(proof.check(), z3.unsat)

    def test_unreachable_array_read_is_pruned_before_scalar_conversion(self):
        text = (Path(__file__).parent / "eqn1f_composed.rec").read_text()
        rec = parse_str(text)
        before = fingerprint(rec)
        i, n = rec.func_sig.children()
        initial = rec.get_base_cases()[0].op[0].decl()
        expected = z3.If(z3.And(i >= 1, n >= i),
                         initial(0) + 10*i*i - 8*i, initial(i))
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   return_value=[expected]) as scalar:
            solve_str(text, multivariate_strategy="tupling")
            target = scalar.call_args.args[0]
            self.assertEqual(len(target.get_rec_cases()), 2)
            self.assertTrue(all(not z3.is_false(case.condition)
                                for case in target.get_rec_cases()))
        self.assertEqual(before, fingerprint(rec))

    def test_integer_infeasible_branch_is_pruned(self):
        n, a, result = z3.Ints("n a result")
        f = z3.Function("f", z3.IntSort(), z3.IntSort(), z3.IntSort())
        previous = z3.Function("previous", z3.IntSort(), z3.IntSort())
        rec = MultiRecurrence(
            f(n, a), [BaseCase(n <= 0, (a,))],
            [RecursiveCase(z3.And(n > 0, n < 1),
                           {result: f(n-1, a)}, (result + previous(n),)),
             RecursiveCase(n >= 1, {result: f(n-1, a)}, (result + 1,))],
        )
        self.assertFalse(z3.is_false(rec.get_rec_cases()[0].condition))
        expected = a + z3.If(n > 0, n, 0)
        with patch("rec_solver.core.solver.parse_str", return_value=rec):
            with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                       return_value=[expected]) as scalar:
                solve_str("", multivariate_strategy="tupling")
                self.assertEqual(len(scalar.call_args.args[0].get_rec_cases()), 1)

    def test_live_array_read_is_not_discarded(self):
        text = (Path(__file__).parent / "eqn1f_composed.rec").read_text()
        text = text.replace("And(i == n, n <= 0)", "And(i == n, n >= 2)")
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   side_effect=RuntimeError("scalar reached")) as scalar:
            with self.assertRaisesRegex(TuplingUnsupported, "input application varies"):
                solve_str(text, multivariate_strategy="tupling")
            scalar.assert_not_called()

    def test_composed_array_recurrence_end_to_end(self):
        text = (Path(__file__).parent / "eqn1f_composed.rec").read_text()
        rec = parse_str(text)
        closed = solve_str(text, multivariate_strategy="tupling")
        i, n = rec.func_sig.children()
        initial = rec.get_base_cases()[0].op[0].decl()
        expected = z3.If(z3.And(i >= 1, n >= i),
                         initial(0) + 10*i*i - 8*i, initial(i))
        proof = z3.Solver()
        proof.set(timeout=5000)
        proof.add(closed.as_dict()[rec.func_sig] != expected)
        self.assertEqual(proof.check(), z3.unsat)

    def test_nearly_tail_fast_path_rejects_invalid_candidate(self):
        text = (Path(__file__).parent / "triangular.rec").read_text()
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   return_value=[z3.IntVal(0)]):
            with self.assertRaisesRegex(TuplingUnsupported, "closed-form candidate"):
                solve_str(text, multivariate_strategy="tupling")

    def test_nearly_tail_fast_path_keeps_termination_check(self):
        text = "if (n<=0) { f(n,a)=a; } else { f(n,a)=f(n+1,a); }"
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail") as scalar:
            with self.assertRaisesRegex(TuplingUnsupported, "ranking"):
                solve_str(text, multivariate_strategy="tupling")
            scalar.assert_not_called()

    def test_multiple_calls_still_require_tuple_construction(self):
        text = (Path(__file__).parent / "fibonacci.rec").read_text()
        self.assertFalse(parse_str(text).is_nearly_tail())
        with patch("rec_solver.core.tupling.tuple_recurrence",
                   side_effect=TuplingUnsupported("tupling reached")) as tupler:
            with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail") as scalar:
                with self.assertRaisesRegex(TuplingUnsupported, "tupling reached"):
                    solve_str(text, multivariate_strategy="tupling")
                tupler.assert_called_once()
                scalar.assert_not_called()

    def test_incorrect_scalar_solver_candidate_is_rejected(self):
        rec = self.fixture("guarded_affine.rec")
        with patch("rec_solver.core.dag_reconstruction.solve_nearly_tail",
                   return_value=[z3.IntVal(0), z3.IntVal(0)]):
            with self.assertRaisesRegex(TuplingUnsupported, "closed-form candidate"):
                solve_str((Path(__file__).parent / "guarded_affine.rec").read_text(),
                          multivariate_strategy="tupling")

    def test_multicall_end_to_end_closed_form(self):
        text = (Path(__file__).parent / "arithmetic_progression.rec").read_text()
        rec = parse_str(text)
        result = self.assert_exact_tuple(rec, itertools.product(range(-3, 20), range(-2, 3)))
        self.assertEqual(len(result.components), 2)
        closed = solve_str(text, multivariate_strategy="tupling")
        n, a = rec.func_sig.children()
        proof = z3.Solver()
        proof.add(closed.as_dict()[rec.func_sig] != n + a)
        self.assertEqual(proof.check(), z3.unsat)

    def test_unresolved_scalar_solver_candidate_is_rejected(self):
        rec = self.fixture("triangular.rec")
        unknown = z3.Function("unresolved", z3.IntSort(), z3.IntSort())
        with self.assertRaisesRegex(TuplingUnsupported, "unresolved"):
            certify_closed_form(rec, unknown(rec.func_sig.arg(0)))

    def test_nonlinear_argument_rejected(self):
        rec = parse_str("if (n<=0) { f(n,a)=a; } else { f(n,a)=f(n-1,a*a); }")
        with self.assertRaisesRegex(TuplingUnsupported, "affine"):
            tuple_recurrence(rec)

    def test_nonlinear_results_rejected(self):
        rec = parse_str("if (n<=1) { f(n,a)=a; } else { f(n,a)=f(n-1,a)*f(n-2,a); }")
        with self.assertRaisesRegex(TuplingUnsupported, "nonlinear"):
            tuple_recurrence(rec)

    def test_noninvertible_anchor_rejected(self):
        rec = parse_str("if (n<=0) { f(n,a)=a; } else { f(n,a)=f(0,a); }")
        # The child is provably a base call, so no inverse is needed.
        self.assert_exact_tuple(rec, itertools.product(range(4), range(3)))
        rec = parse_str("if (n<=0) { f(n,a)=a; } else { f(n,a)=f(2*n,a); }")
        with self.assertRaisesRegex(TuplingUnsupported, "invertible"):
            tuple_recurrence(rec)

    def test_no_termination_certificate(self):
        rec = parse_str("if (n<=0) { f(n,a)=a; } else { f(n,a)=f(n+1,a); }")
        with self.assertRaisesRegex(TuplingUnsupported, "ranking"):
            tuple_recurrence(rec)

    def test_component_limit(self):
        with self.assertRaisesRegex(TuplingUnsupported, "component limit"):
            tuple_recurrence(self.fixture("fibonacci.rec"), TuplingOptions(max_components=1))

    def test_round_limit(self):
        with self.assertRaisesRegex(TuplingUnsupported, "round limit"):
            tuple_recurrence(self.fixture("fibonacci.rec"), TuplingOptions(max_rounds=1))

    def test_branch_limit(self):
        with self.assertRaisesRegex(TuplingUnsupported, "branch limit"):
            tuple_recurrence(self.fixture("fibonacci.rec"), TuplingOptions(max_branches=1))

    def test_invalid_limits(self):
        with self.assertRaises(ValueError):
            TuplingOptions(max_rounds=0)

    def test_solver_unknown_rejected(self):
        rec = self.fixture("fibonacci.rec")
        with patch("z3.Solver.check", return_value=z3.unknown):
            with self.assertRaisesRegex(TuplingUnsupported, "unknown"):
                tuple_recurrence(rec)

    def test_partial_source_rejected(self):
        n, a = z3.Ints("n a")
        f = z3.Function("partial_f", z3.IntSort(), z3.IntSort(), z3.IntSort())
        rec = MultiRecurrence(f(n, a), [BaseCase(n == 0, (a,))],
                              [RecursiveCase(n > 0, {a: f(n-1, a)}, (a,))])
        with self.assertRaisesRegex(TuplingUnsupported, "cover"):
            tuple_recurrence(rec)

    def test_array_frame_tuple_growth_is_explicit(self):
        with self.assertRaises(TuplingUnsupported):
            tuple_recurrence(self.fixture("array_fibonacci.rec"),
                             TuplingOptions(max_components=3))

    def test_pivot_dispatch_unchanged(self):
        text = (Path(__file__).parent / "triangular.rec").read_text()
        with patch("rec_solver.core.solver.solve_multivariate_rec", return_value="pivot") as original:
            self.assertEqual(solve_str(text), "pivot")
            original.assert_called_once()

    def test_no_implicit_pivot_fallback(self):
        text = (Path(__file__).parent / "fibonacci.rec").read_text()
        with patch("rec_solver.core.solver.solve_multivariate_rec") as pivot:
            with self.assertRaises(TuplingUnsupported):
                solve_str(text, multivariate_strategy="tupling",
                          tupling_options=TuplingOptions(max_components=1))
            pivot.assert_not_called()

    def test_invalid_backend(self):
        with self.assertRaises(ValueError):
            solve_str("", multivariate_strategy="typo")

    def test_environment_dispatch(self):
        import solver
        with patch.dict(os.environ, {"ARITHEXE_MULTIVARIATE_STRATEGY": "tupling"}):
            self.assertEqual(solver.solver_options()["multivariate_strategy"], "tupling")

    def test_worker_protocol_uses_tupling(self):
        recurrence = (Path(__file__).parent / "triangular.rec").read_bytes()
        request = f"SOLVE 7 0 {len(recurrence)}\n".encode() + recurrence + b"QUIT\n"
        env = dict(os.environ, ARITHEXE_MULTIVARIATE_STRATEGY="tupling")
        process = subprocess.run([sys.executable, "-u", str(ROOT / "solver_worker.py")],
                                 input=request, capture_output=True, cwd=ROOT,
                                 env=env, timeout=30, check=True)
        header, payload = process.stdout.split(b"\n", 1)
        status, request_id, size = header.split()
        self.assertEqual((status, request_id), (b"OK", b"7"), process.stderr)
        self.assertEqual(int(size), len(payload))
        proof = z3.Solver()
        proof.from_string(payload.decode())
        f = z3.Function("f", z3.IntSort(), z3.IntSort())
        proof.add(z3.Int("n") == 5, z3.Int("a") == 2, f(5) != 17)
        self.assertEqual(proof.check(), z3.unsat)


if __name__ == "__main__":
    unittest.main()
