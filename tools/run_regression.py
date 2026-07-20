#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path


DEFAULT_CASES = [
    ("test_algebra", None),
    ("test_logic", None),
    ("test_engine", "BENCHMARK_ARRAYS_LOOP.true_1"),
    ("test_engine", "BENCHMARK_ARRAYS_LOOP.true_2"),
    ("test_engine", "BENCHMARK_LOOPS.true_1"),
    ("test_engine", "BENCHMARK_LOOPS.true_3"),
    ("test_engine", "BENCHMARK_NLA.true_3"),
    ("test_engine", "BENCHMARK_NLA.true_4"),
    ("test_engine", "BENCHMARK_LOOPS.true_bounded_cfinite_map_fixed"),
    ("test_engine", "BENCHMARK_BOUNDED_CFINITE.bcf_01_square_invariant"),
    ("test_witness", "WitnessTest.WritesViolationWitnessForRecursiveCounterexample"),
    ("test_witness", "WitnessTest.SnapshotsLoopEntryValueInGhostVariable"),
]


def classify(returncode, output):
    if returncode == 0:
        return "PASS"
    if "frontend-ir-parse" in output or "Failed to parse LLVM IR" in output:
        return "FRONTEND_ERROR"
    if "frontend-command" in output or "Fail to convert" in output:
        return "FRONTEND_ERROR"
    if "recurrence solver" in output or "Error solving recurrence" in output:
        return "SOLVER_ERROR"
    if "verification result is unknown" in output or "VERIUNKNOWN" in output:
        return "UNKNOWN"
    if "Assertion" in output or "[  FAILED  ]" in output:
        return "ASSERTION_FAILURE"
    return "PROCESS_FAILURE"


def run_case(build_dir, binary, gtest_filter, timeout):
    executable = build_dir / binary
    if not executable.exists():
        return "MISSING", f"{executable} does not exist"

    cmd = [str(executable)]
    if gtest_filter:
        cmd.append(f"--gtest_filter={gtest_filter}")

    env = os.environ.copy()
    env.setdefault("ARITHEXE_SOLVER_TIMEOUT_MS", "60000")
    proc = subprocess.run(
        cmd,
        cwd=build_dir,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    return classify(proc.returncode, proc.stdout), proc.stdout


def parse_case(raw):
    if ":" not in raw:
        return raw, None
    binary, gtest_filter = raw.split(":", 1)
    return binary, gtest_filter or None


def main():
    parser = argparse.ArgumentParser(
        description="Run selected verifier regression tests and classify failures."
    )
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "cases",
        nargs="*",
        help="Optional cases as binary or binary:gtest_filter.",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = (root / args.build_dir).resolve()
    cases = [parse_case(case) for case in args.cases] or DEFAULT_CASES

    failed = False
    for binary, gtest_filter in cases:
        label = binary if gtest_filter is None else f"{binary}:{gtest_filter}"
        try:
            status, output = run_case(build_dir, binary, gtest_filter, args.timeout)
        except subprocess.TimeoutExpired as error:
            status = "TIMEOUT"
            output = error.stdout or ""
        print(f"{status:18} {label}")
        if status != "PASS":
            failed = True
            tail = "\n".join(output.splitlines()[-12:])
            if tail:
                print(tail)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
