#!/usr/bin/env python3
"""Run array-fpi reachability tasks with reproducible, bounded local resources."""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
from queue import Queue
import re
import shutil
import signal
import subprocess
import sys
import time

import yaml


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True)


def load_tasks(svcomp):
    tasks = []
    for metadata in sorted((svcomp / "c/array-fpi").glob("*.yml")):
        document = yaml.safe_load(metadata.read_text())
        properties = [p for p in document.get("properties", [])
                      if p["property_file"].endswith("unreach-call.prp")]
        if not properties:
            continue
        if len(properties) != 1 or type(properties[0]["expected_verdict"]) is not bool:
            raise ValueError(f"ambiguous reachability task: {metadata}")
        inputs = document["input_files"]
        inputs = inputs if isinstance(inputs, list) else [inputs]
        sources = [p.resolve() for pattern in inputs for p in metadata.parent.glob(pattern)]
        if len(sources) != 1:
            raise ValueError(f"expected one C input: {metadata}")
        prop = (metadata.parent / properties[0]["property_file"]).resolve()
        if not prop.is_file():
            raise ValueError(f"missing property file: {prop}")
        model = document.get("options", {}).get("data_model", "ILP32")
        if model not in {"ILP32", "LP64"}:
            raise ValueError(f"unsupported data model: {model}")
        tasks.append(dict(name=metadata.stem, metadata=str(metadata), source=str(sources[0]),
                          property_file=str(prop), expected=properties[0]["expected_verdict"],
                          data_model=model, source_sha256=sha256(sources[0]),
                          metadata_sha256=sha256(metadata), property_sha256=sha256(prop)))
    if not tasks:
        raise ValueError("no array-fpi reachability tasks found")
    return tasks


def verdict(log):
    answers = []
    for line in log.splitlines():
        token = line.strip()
        if token in {"TRUE", "UNKNOWN"}:
            answers.append(token)
        elif re.fullmatch(r"FALSE(?:\([A-Za-z0-9_-]+\))?", token):
            answers.append("FALSE")
    return answers[-1] if answers else None


def classify(row, log):
    if row["status"] == "timeout":
        return "TIMEOUT"
    if row["status"] != "finished":
        return "LAUNCH_ERROR"
    if row["returncode"] != 0:
        if "bad_alloc" in log or "Cannot allocate memory" in log or "out of memory" in log.lower():
            return "MEMORY_ERROR"
        if "Assertion " in log and "failed." in log:
            return "ASSERTION_FAILURE"
        if "Verification stopped" in log:
            return "VERIFIER_ERROR"
        return "PROCESS_ERROR"
    if row["verdict"] in {"TRUE", "FALSE"}:
        actual = row["verdict"] == "TRUE"
        return ("CORRECT_SAFE" if actual else "CORRECT_UNSAFE") if actual == row["expected"] else (
            "WRONG_SAFE" if actual else "WRONG_UNSAFE")
    return "UNKNOWN" if row["verdict"] == "UNKNOWN" else "NO_VERDICT"


def summarize(rows, total, start):
    return dict(completed=len(rows), total=total,
                elapsed_seconds=round(time.monotonic() - start, 3),
                counts=dict(sorted(Counter(r["classification"] for r in rows).items())),
                expected_safe_completed=sum(r["expected"] for r in rows),
                expected_unsafe_completed=sum(not r["expected"] for r in rows),
                results=sorted(rows, key=lambda r: r["name"]))


def save_summary(path, summary):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(summary, indent=2) + "\n")
    temporary.replace(path)


def reclassify_results(directory):
    """Audit stored logs without rerunning or changing any verification task."""
    path = directory / "summary.json"
    summary = json.loads(path.read_text())
    if not summary.get("complete"):
        raise ValueError("wait until execution is complete before reclassifying")
    raw = directory / "summary.execution.json"
    if not raw.exists():
        shutil.copyfile(path, raw)
    for row in summary["results"]:
        log = Path(row["log"]).read_text(errors="replace")
        row["verdict"] = verdict(log)
        row["classification"] = classify(row, log)
        messages = []
        for line in log.splitlines():
            for marker in ["Verification stopped", "Unknown reason:", "Assertion "]:
                if marker in line:
                    messages.append(line[line.index(marker):])
        row["diagnostic"] = messages[-1] if messages else None
        (Path(row["log"]).parent / "result.json").write_text(json.dumps(row, indent=2) + "\n")
    summary["counts"] = dict(sorted(Counter(r["classification"] for r in summary["results"]).items()))
    summary["failure_reasons"] = dict(Counter(
        r["diagnostic"] for r in summary["results"]
        if r["diagnostic"] and r["classification"] not in {
            "CORRECT_SAFE", "CORRECT_UNSAFE", "WRONG_SAFE", "WRONG_UNSAFE"}))
    (directory / "results.audited.jsonl").write_text("".join(
        json.dumps(row) + "\n" for row in summary["results"]))
    summary["classification_audit"] = dict(classifier_sha256=sha256(Path(__file__).resolve()),
                                            note="Parses FALSE(unreach-call); raw execution summary preserved.")
    save_summary(path, summary)
    print(json.dumps({k: v for k, v in summary.items() if k != "results"}), flush=True)


def run_task(task, args, cpus, env):
    cpu = cpus.get()
    start = time.monotonic()
    directory = args.output_dir / "tasks" / task["name"]
    directory.mkdir()
    log_path = directory / "output.log"
    command = [shutil.which("taskset"), "-c", str(cpu), shutil.which("prlimit"),
               f"--as={args.memory_mib * 1024 * 1024}", "--core=0", "--",
               str(args.binary), "--no-witness", f"--data-model={task['data_model']}",
               "--property-file", task["property_file"], task["source"]]
    row = dict(task, cpu=cpu, command=command, log=str(log_path))
    process = None
    try:
        with log_path.open("w") as stream:
            process = subprocess.Popen(command, cwd=directory, env=env,
                                       stdout=stream, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                code = process.wait(timeout=args.timeout)
                row.update(status="finished", returncode=code)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                row.update(status="timeout", returncode=124)
    except OSError as error:
        row.update(status="launch-error", returncode=127, error=str(error))
    finally:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        cpus.put(cpu)
    log = log_path.read_text(errors="replace")
    row.update(seconds=round(time.monotonic() - start, 3), verdict=verdict(log),
               solver_succeeded="Recurrence solved successfully" in log,
               solver_failed="Failed to solve recurrence" in log,
               tupling_rejected="TuplingUnsupported" in log)
    row["classification"] = classify(row, log)
    (directory / "result.json").write_text(json.dumps(row, indent=2) + "\n")
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--svcomp", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=Path("build/arith_exe"))
    parser.add_argument("--strategy", choices=["tupling", "pivot"], default="tupling")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--solver-timeout-ms", type=int, default=60000)
    parser.add_argument("--memory-mib", type=int, default=3072)
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.svcomp = args.svcomp.resolve()
    args.output_dir = args.output_dir.resolve()
    args.binary = args.binary.resolve()
    if (min(args.timeout, args.solver_timeout_ms, args.memory_mib, args.workers) <= 0
            or args.workers > len(os.sched_getaffinity(0))):
        parser.error("invalid resource limits or unavailable worker CPUs")
    if not args.binary.is_file() or not shutil.which("taskset") or not shutil.which("prlimit"):
        parser.error("binary, taskset, and prlimit are required")
    tasks = load_tasks(args.svcomp)
    env = os.environ.copy()
    env.update(ARITHEXE_MULTIVARIATE_STRATEGY=args.strategy,
               ARITHEXE_SOLVER_TIMEOUT_MS=str(args.solver_timeout_ms),
               ARITHEXE_SOLVER_PYTHON=sys.executable,
               ARITHEXE_SOLVER_WORKER=str(root / "solver_worker.py"),
               OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1", MKL_NUM_THREADS="1")
    # Do not inherit an earlier experiment's semantic relaxations or tracing.
    for key in ["ARITHEXE_FORCE_PATH_EXPRESSIONS", "ARITHEXE_SOLVER_TRACE_DIR"]:
        env.pop(key, None)
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / "tasks").mkdir()
    implementation = [root / "solver.py", root / "solver_worker.py", Path(__file__).resolve()]
    implementation += sorted((root / "rec_solver").rglob("*.py"))
    manifest = dict(strategy=args.strategy, task_count=len(tasks),
                    expected_safe=sum(t["expected"] for t in tasks),
                    expected_unsafe=sum(not t["expected"] for t in tasks),
                    wall_timeout_seconds=args.timeout, solver_timeout_ms=args.solver_timeout_ms,
                    address_space_limit_mib_per_process=args.memory_mib,
                    workers=args.workers, witness_validation=False,
                    integer_semantics="default fixed-width; no relaxation flag",
                    git_head=git(root, "rev-parse", "HEAD").strip(),
                    git_branch=git(root, "branch", "--show-current").strip(),
                    git_status=git(root, "status", "--short"),
                    binary=str(args.binary), binary_sha256=sha256(args.binary),
                    python=sys.executable,
                    environment={k: v for k, v in env.items() if k.startswith("ARITHEXE_")},
                    implementation_sha256={str(p.relative_to(root)): sha256(p) for p in implementation},
                    tasks=tasks)
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({k: manifest[k] for k in ["task_count", "expected_safe", "expected_unsafe",
                                               "strategy", "wall_timeout_seconds", "workers"]}), flush=True)
    cpus = Queue()
    for cpu in sorted(os.sched_getaffinity(0))[:args.workers]:
        cpus.put(cpu)
    start, rows = time.monotonic(), []
    save_summary(args.output_dir / "summary.json", summarize(rows, len(tasks), start))
    with (args.output_dir / "results.jsonl").open("w") as journal:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = [pool.submit(run_task, task, args, cpus, env) for task in tasks]
            for future in as_completed(futures):
                row = future.result()
                rows.append(row)
                journal.write(json.dumps(row) + "\n")
                journal.flush()
                summary = summarize(rows, len(tasks), start)
                save_summary(args.output_dir / "summary.json", summary)
                print(f"{len(rows):3}/{len(tasks)} {row['name']:30} "
                      f"{row['classification']:15} {row['seconds']:6.2f}s {summary['counts']}", flush=True)
    summary = summarize(rows, len(tasks), start)
    summary["complete"] = True
    save_summary(args.output_dir / "summary.json", summary)
    print(json.dumps({k: v for k, v in summary.items() if k != "results"}), flush=True)
    return 1 if any(r["classification"].startswith("WRONG_") for r in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
