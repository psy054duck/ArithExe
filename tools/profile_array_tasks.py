"""Capture request profiles and recurrences; not a benchmark timing harness."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--task", action="append", required=True)
    parser.add_argument("--trace-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    dataset = ROOT.parent / "svcomp25/c"
    results = []
    for task in args.task:
        directory = output / task
        directory.mkdir()
        env = {key: os.environ[key] for key in ("PATH", "LANG", "LD_LIBRARY_PATH")
               if key in os.environ}
        env.update(ARITHEXE_SOLVER_PYTHON=str(ROOT.parent / "solver-venv/bin/python"),
                   ARITHEXE_SOLVER_WORKER=str(ROOT / "tools/profile_solver_worker.py"),
                   ARITHEXE_SOLVER_TIMEOUT_MS="300000",
                   ARITHEXE_MULTIVARIATE_STRATEGY="tupling",
                   ARITHEXE_FORCE_PATH_EXPRESSIONS="0", PYTHONHASHSEED="0",
                   OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1", MKL_NUM_THREADS="1",
                   ARITHEXE_REQUEST_PROFILE_DIR=str(directory / "requests"))
        if args.trace_only:
            env["ARITHEXE_SOLVER_WORKER"] = str(ROOT / "solver_worker.py")
            env["ARITHEXE_SOLVER_TRACE_DIR"] = str(directory / "requests")
        command = [str(ROOT / "build/arith_exe"), "--no-witness", "--data-model=ILP32",
                   "--property-file", str(dataset / "properties/unreach-call.prp"),
                   str(dataset / "array-fpi" / (task + ".c"))]
        start = time.monotonic()
        timed_out = False
        with (directory / "verification.log").open("w") as stream:
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=360)
            except subprocess.TimeoutExpired:
                import signal
                os.killpg(process.pid, signal.SIGKILL)
                code = process.wait()
                timed_out = True
        row = dict(task=task, returncode=code, timed_out=timed_out,
                   profiled_wall_seconds=time.monotonic() - start)
        results.append(row)
        (output / "status.json").write_text(json.dumps(results, indent=2))
        print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main()
