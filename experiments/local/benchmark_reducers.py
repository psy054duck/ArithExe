#!/usr/bin/env python3
"""Local reducer-subset diagnostics using the existing macOS benchmark harness.

Reports tool verdicts, not validated correctness. Source C programs are never run.
"""
import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import shutil
import time


def load_runner(path):
    spec = importlib.util.spec_from_file_location("local_benchmark", path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    return runner


def main():
    repository = Path(__file__).resolve().parents[2]
    workspace = repository.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=workspace / "svcomp2026-single-assertions")
    parser.add_argument("--repository", type=Path, default=repository)
    parser.add_argument("--tool", type=Path, default=repository / "build/arith_exe")
    parser.add_argument("--runner", type=Path,
                        default=workspace / "svcomp2026-final-assertions/benchmarking/benchmark.py")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--memory-mib", type=int, default=1024)
    parser.add_argument("--solver-python", default="/opt/miniconda3/envs/veri/bin/python")
    parser.add_argument("--clang", default="/opt/homebrew/opt/llvm/bin/clang")
    parser.add_argument("--mode", choices=["proposed", "fixed-width"], default="proposed")
    parser.add_argument("--icra-results", type=Path,
                        help="Select only tasks reported TRUE in this ICRA JSONL baseline")
    parser.add_argument("--stop-on-failure", action="store_true",
                        help="Run sequentially and stop at the first verdict other than TRUE (requires --jobs 1)")
    parser.add_argument("--limit", type=int, help="Smoke-test limit; omit to test every reducer task")
    args = parser.parse_args()
    if min(args.timeout, args.jobs, args.memory_mib) <= 0:
        parser.error("limits must be positive")
    if args.stop_on_failure and args.jobs != 1:
        parser.error("--stop-on-failure requires --jobs 1; no later tasks may be queued")
    output = args.output.resolve()
    if output.exists():
        parser.error("choose a fresh output directory")
    dataset = args.dataset.resolve()
    selection_path = dataset / "benchmark-selection.json"
    selection = json.loads(selection_path.read_text()) if selection_path.exists() else None
    excluded_components = selection["excluded_components"] if selection else []
    excluded_tasks = selection.get("excluded_tasks", []) if selection else []
    rows = [row for row in csv.DictReader((dataset / "manifest.csv").open())
            if row["status"] == "included" and "-reducer" in row["task"] and
            row["component"] not in excluded_components and row["task"] not in excluded_tasks]
    icra_path = args.icra_results.resolve() if args.icra_results else None
    if icra_path:
        proven = defaultdict(list)
        for record in map(json.loads, icra_path.read_text().splitlines()):
            if record["tool"] in {"icra_split", "icra_refine"} and record["verdict"] == "TRUE":
                proven[record["task"]].append(record)
        rows = sorted((row for row in rows if row["task"] in proven), key=lambda row: row["task"])
        for row in rows:
            if any(record["source_sha256"] != row["processed_sha256"] or
                   record["data_model"] != row["data_model"] for record in proven[row["task"]]):
                parser.error("ICRA input hash/data-model mismatch: " + row["task"])
    if args.limit is not None:
        if args.limit <= 0:
            parser.error("limit must be positive")
        rows = rows[:args.limit]
    if not rows:
        parser.error("no reducer tasks selected")
    runner = load_runner(args.runner.resolve())
    sampler = runner.ResourceSampler(.1)
    sampler.start()  # Check ps permissions before creating output or running tools.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    started = time.monotonic()
    try:
        output.mkdir(parents=True)
        snapshot = runner.freeze(output, dataset, args.repository.resolve(), args.tool.resolve())
        shutil.copy2(__file__, output / "benchmark_reducers.py")
        if icra_path:
            shutil.copy2(icra_path, output / "icra-baseline-results.jsonl")
        transforms = {row["task"]: row for row in
                      json.loads((snapshot / "dataset/transformations.json").read_text())}
        environment = dict(
            ARITHEXE_FORCE_PATH_EXPRESSIONS="1", ARITHEXE_PATH_MAX_ROOT="6",
            ARITHEXE_PATH_BEAM="4", ARITHEXE_PATH_EVIDENCE="2",
            ARITHEXE_COARSE_FALLBACK="1", ARITHEXE_RESIDUE_FALLBACK="1",
            ARITHEXE_COARSE_TIMEOUT_MS="3000", ARITHEXE_COARSE_REFINE_STEPS="512",
            ARITHEXE_COARSE_POLY_EXPR_STRATEGY="auto", ARITHEXE_COARSE_POLY_EXPR_ORDER="1",
            ARITHEXE_SOLVER_PYTHON=str(Path(args.solver_python).resolve()),
            ARITHEXE_SOLVER_WORKER=str(snapshot / "ArithExe/solver_worker.py"),
            ARITHEXE_COARSE_LEMMA_FILE=str(snapshot / "ArithExe/lemma.smt2"),
            ARITHEXE_CLANG=str(Path(args.clang).resolve()))
        binary = snapshot / "bin/arith_exe"
        configuration = dict(
            created_utc=runner.utc(), dataset=str(dataset), mode=args.mode,
            timeout_seconds=args.timeout, jobs=args.jobs, memory_limit_mib_sampled=args.memory_mib,
            tasks=len(rows), parents=len({row["parent_task"] for row in rows}),
            icra_results=str(icra_path) if icra_path else None,
            icra_results_sha256=runner.sha(output / "icra-baseline-results.jsonl") if icra_path else None,
            stop_on_failure=args.stop_on_failure,
            selected_tasks=rows, environment=environment, binary_sha256=runner.sha(binary),
            benchmark_selection=selection,
            benchmark_selection_sha256=runner.sha(selection_path) if selection else None,
            manifest_sha256=runner.sha(snapshot / "dataset/manifest.csv"),
            runner_sha256=runner.sha(output / "benchmark.py"),
            driver_sha256=runner.sha(output / "benchmark_reducers.py"),
            platform=runner.platform.platform(),
            hardware=runner.command_output(["/usr/sbin/sysctl", "hw.memsize", "hw.logicalcpu", "hw.model"]),
            metric_scope="local wall time; wait4 rusage; sampled process-group RSS, not hard cgroup isolation",
            verdict_scope="reported verdicts; no applicable ground truth or witness validation claimed")
        runner.atomic_json(output / "configuration.json", configuration)
        results = []

        first_failure = None

        def export(complete=False):
            grouped = defaultdict(list)
            for result in results:
                grouped[result["parent_task"]].append(result)
            selected_counts = Counter(row["parent_task"] for row in rows)
            summary = dict(
                complete=complete, updated_utc=runner.utc(), total=len(rows), completed=len(results),
                stopped_on_failure=first_failure is not None, first_failure=first_failure,
                not_run=len(rows) - len(results),
                status_counts=dict(Counter(r["status"] for r in results)),
                elapsed_seconds=round(time.monotonic() - started, 3),
                witness_count=sum(r["witness_exists"] for r in results),
                residue_proofs=sum(r["residue_proofs"] for r in results),
                parent_results={name: dict(total=selected_counts[name], completed=len(grouped[name]),
                    statuses=dict(Counter(r["status"] for r in grouped[name])),
                    all_assertions_reported_true=len(grouped[name]) == selected_counts[name] and
                        all(r["status"] == "TRUE" for r in grouped[name]))
                    for name in sorted(selected_counts)},
                sampler_errors=list(sampler.errors),
                verdict_scope=configuration["verdict_scope"])
            runner.atomic_json(output / "summary.json", summary)
            if results:
                with (output / "results.csv").open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(results[0]), extrasaction="ignore")
                    writer.writeheader()
                    writer.writerows(sorted(results, key=lambda r: r["task"]))

        def execute(row):
            task = row["task"]
            source = snapshot / "dataset/tasks" / transforms[task]["input_files"][0]
            if runner.sha(source) != row["processed_sha256"]:
                raise RuntimeError("input hash mismatch: " + task)
            directory = output / "runs" / task.removeprefix("c/").removesuffix(".yml")
            command = [str(binary), "--verbose", "--data-model=" + row["data_model"],
                       "--property-file=" + str(snapshot / "dataset/tasks/c/properties/unreach-call.prp"),
                       "--witness=" + str(directory / "witness.yml")]
            if args.mode == "proposed":
                command.append("--ignore-bitwidth-constraints")
            command.append(str(source))
            env = {k: v for k, v in os.environ.items() if not k.startswith("ARITHEXE_")}
            env.update(environment)
            result = runner.run_tool(command, env, directory, sampler, args.timeout,
                                     args.memory_mib * 1024**2, 1024**2)
            text = Path(result["log"]).read_text(errors="replace")
            result.update(task=task, parent_task=row["parent_task"], component=row["component"],
                          assertion_index=int(row["assertion_index"]), mode=args.mode,
                          data_model=row["data_model"], source_sha256=row["processed_sha256"],
                          original_expected_verdict_provenance=row["original_expected_verdict"],
                          residue_proofs=len(re.findall(r'\[residue-path\] proved guarded CFG fixed point:', text)),
                          coarse_summaries=len(re.findall(r'\[coarse-path\] Summarized loop ', text)))
            runner.atomic_json(directory / "result.json", result)
            return result

        export()
        with (output / "results.jsonl").open("a", buffering=1) as journal:
            def record_result(result):
                nonlocal first_failure
                journal.write(json.dumps(result, sort_keys=True) + "\n")
                journal.flush()
                os.fsync(journal.fileno())
                results.append(result)
                if args.stop_on_failure and result["status"] != "TRUE":
                    first_failure = dict(task=result["task"], status=result["status"])
                export()
                print(f'{len(results)}/{len(rows)} {result["status"]} '
                      f'{result["wall_seconds"]:.3f}s {result["task"]}', flush=True)

            if args.stop_on_failure:
                for row in rows:
                    record_result(execute(row))
                    if first_failure:
                        break
            else:
                with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                    futures = [pool.submit(execute, row) for row in rows]
                    for future in as_completed(futures):
                        record_result(future.result())
        if runner.sha(binary) != configuration["binary_sha256"]:
            raise RuntimeError("frozen binary changed during run")
        export(complete=len(results) == len(rows))
        runner.atomic_json(output / ("stopped.json" if first_failure else "completed.json"),
                           dict(finished_utc=runner.utc(), tasks=len(results), first_failure=first_failure))
    finally:
        sampler.close()


if __name__ == "__main__":
    main()
