#!/usr/bin/env python3
"""Record small verifier comparisons; do not treat this as a full evaluation."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def verdict_lines(log):
    return [line for line in log.splitlines()
            if line.strip() in {"TRUE", "FALSE", "UNKNOWN"}
            or any(word in line for word in ["VERISAFE", "VERIUNSAFE", "VERIUNKNOWN"])][-5:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--svcomp", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--case", type=Path, action="append",
                        help="Run only these C sources instead of the default smoke set")
    parser.add_argument("--strategy", choices=["pivot", "tupling"], action="append")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [root / "test/tupling/true_triangular.c",
             root / "test/tupling/true_tupled_linear.c"]
    cases += [args.svcomp.resolve() / "c" / name for name in
              ["array-fpi/condm.c", "array-fpi/pcomp.c", "recursive/Fibonacci02.c"]]
    if args.case:
        cases = [p.resolve() for p in args.case]
    rows = []
    for source in cases:
        for strategy in (args.strategy or ["pivot", "tupling"]):
            env = dict(os.environ, ARITHEXE_MULTIVARIATE_STRATEGY=strategy)
            env.setdefault("ARITHEXE_SOLVER_TIMEOUT_MS", "10000")
            command = [str(build / "arith_exe"), "--no-witness", "--data-model=ILP32",
                       "--property-file", str(args.svcomp.resolve() / "c/properties/unreach-call.prp"),
                       str(source)]
            start = time.monotonic()
            row = {"source": str(source), "strategy": strategy, "command": command}
            try:
                # A process group ensures a timeout also terminates solver children.
                process = subprocess.Popen(command, cwd=build, env=env, text=True,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    log, _ = process.communicate(timeout=args.timeout)
                    row.update(status="finished", returncode=process.returncode)
                except subprocess.TimeoutExpired:
                    import signal
                    os.killpg(process.pid, signal.SIGKILL)
                    log, _ = process.communicate()
                    row.update(status="timeout", returncode=124)
            except OSError as error:
                log = str(error)
                row.update(status="launch-error", returncode=127)
            row["seconds"] = round(time.monotonic() - start, 3)
            row["verdict_lines"] = verdict_lines(log)
            (output / f"{source.stem}-{strategy}.log").write_text(log)
            rows.append(row)
            print(json.dumps(row), flush=True)
    (output / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
