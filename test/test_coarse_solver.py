"""Run: python -m unittest discover -s test -p test_coarse_solver.py."""
import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import z3
from solver import solve_coarse_str_to_smt2

CUBIC = """x(0)=x0; y(0)=y0;
if (true) {x(n+1)=x(n)+1; y(n+1)=y(n)+3*x(n)*x(n)+3*x(n)+1;}
"""


RECURRENCE = """x(0)=x0; y(0)=y0;
if (choice==0) {x(n+1)=x(n)+1; y(n+1)=y(n);}
else {x(n+1)=x(n); y(n+1)=y(n)+1;}
"""


class CoarseSolver(unittest.TestCase):
    def test_common_sum_growth_not_individual_branch_solutions(self):
        assertions = z3.parse_smt2_string(solve_coarse_str_to_smt2(RECURRENCE))
        x, y, x0, y0, n = z3.Ints("x y x0 y0 n")
        solver = z3.Solver()
        solver.add(assertions, x + y != x0 + y0 + n)
        self.assertEqual(solver.check(), z3.unsat)
        # Neither branch is privileged by the result.
        for a, b in [(5, 0), (0, 5), (2, 3)]:
            solver = z3.Solver()
            solver.add(assertions, x0 == 0, y0 == 0, n == 5, x == a, y == b)
            self.assertEqual(solver.check(), z3.sat)

    def test_fast_defaults_are_independent_of_legacy_cli(self):
        with patch.dict(os.environ, {"ARITHEXE_POLY_EXPR_STRATEGY": "general",
                                     "ARITHEXE_POLY_EXPR_ORDER": "4"}):
            self.test_common_sum_growth_not_individual_branch_solutions()

    def test_default_degree_is_two_not_first_useful_degree(self):
        from rec_solver.core.closed_form import ExprClosedForm
        with patch.dict(os.environ, {}, clear=True), patch(
                "rec_solver.core.poly_expr.poly_expr_solving") as synthesis:
            synthesis.return_value = ExprClosedForm({}, z3.Int("n"))
            solve_coarse_str_to_smt2(RECURRENCE)
            self.assertEqual(synthesis.call_count, 1)
            self.assertEqual(synthesis.call_args.args[1], 2)

    def test_explicit_degree_controls_observable_strength(self):
        x, y, x0, y0, n = z3.Ints("x y x0 y0 n")
        for degree in (2, 3):
            constraints = z3.parse_smt2_string(solve_coarse_str_to_smt2(CUBIC, degree=degree))
            solver = z3.Solver()
            solver.set(timeout=3000)
            solver.add(constraints, n >= 0, y-x*x*x != y0-x0*x0*x0)
            self.assertEqual(solver.check(), z3.sat if degree == 2 else z3.unsat)

    def test_worker_degree_is_per_request_not_frozen_environment(self):
        worker = Path(__file__).resolve().parents[1] / "solver_worker.py"
        requests = b""
        for request_id, degree in ((1, 2), (2, 3), (3, 2)):
            data = CUBIC.encode()
            requests += f"COARSE {request_id} 1 {len(data)} {degree}\n".encode() + b"n" + data
        requests += b"QUIT\n"
        environment = dict(os.environ, ARITHEXE_COARSE_POLY_EXPR_DEGREE="1")
        result = subprocess.run([sys.executable, "-u", str(worker)], input=requests,
                                capture_output=True, timeout=15, check=True, env=environment)
        payload = result.stdout
        x, y, x0, y0, n = z3.Ints("x y x0 y0 n")
        for request_id, expected in ((1, z3.sat), (2, z3.unsat), (3, z3.sat)):
            header, payload = payload.split(b"\n", 1)
            status, response_id, size = header.split()
            self.assertEqual(status, b"OK", result.stderr.decode())
            self.assertEqual(int(response_id), request_id)
            assertions = z3.parse_smt2_string(payload[:int(size)].decode())
            payload = payload[int(size):]
            solver = z3.Solver()
            solver.set(timeout=3000)
            solver.add(assertions, n >= 0, y-x*x*x != y0-x0*x0*x0)
            self.assertEqual(solver.check(), expected)
        self.assertEqual(payload, b"")

    def test_worker_accepts_both_modes_in_one_session(self):
        worker = Path(__file__).resolve().parents[1] / "solver_worker.py"
        precise = "x(0)=0; if (true) {x(n+1)=x(n)+1;}"
        requests = b""
        for request_id, mode, recurrence in [(1, "SOLVE", precise),
                                             (2, "COARSE", RECURRENCE)]:
            data = recurrence.encode()
            requests += f"{mode} {request_id} 1 {len(data)}\n".encode() + b"n" + data
        requests += b"QUIT\n"
        result = subprocess.run([sys.executable, "-u", str(worker)],
                                input=requests, capture_output=True, timeout=15,
                                check=True)
        payload = result.stdout
        for request_id in [1, 2]:
            header, payload = payload.split(b"\n", 1)
            status, response_id, size = header.split()
            self.assertEqual(status, b"OK", result.stderr.decode())
            self.assertEqual(int(response_id), request_id)
            z3.parse_smt2_string(payload[:int(size)].decode())
            payload = payload[int(size):]
        self.assertEqual(payload, b"")


if __name__ == "__main__":
    unittest.main()
