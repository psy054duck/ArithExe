#!/usr/bin/env python3
"""Run array benchmarks with BenchExec resource accounting and result tables."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import time
from xml.etree import ElementTree as ET

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.run_array_fpi import load_tasks
from tools.array_tasks import arrays_definition, load_arrays


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def interpreter_path(path):
    # Resolving the final symlink would bypass the virtual environment.
    return Path(os.path.abspath(path))


def integer_tasks(tasks, directory):
    """Keep C task labels as provenance, not integer-model ground truth."""
    directory.mkdir(parents=True, exist_ok=False)
    execution_tasks = []
    for task in tasks:
        document = yaml.safe_load(Path(task["metadata"]).read_text())
        document["input_files"] = task["source"]
        document["properties"] = [{"property_file": task["property_file"]}]
        if 'required_files' in document:
            required = document['required_files']
            required = required if isinstance(required, list) else [required]
            parent = Path(task['metadata']).parent
            document['required_files'] = [str((parent / pattern).resolve()) for pattern in required]
        metadata = directory / (task["name"] + ".yml")
        metadata.write_text(yaml.safe_dump(document, sort_keys=False))
        execution_tasks.append(dict(task, metadata=str(metadata),
                                    metadata_sha256=digest(metadata), expected=None))
    return execution_tasks


def make_definition(tasks, args):
    integer = getattr(args, "semantics", "c") == "integer"
    definition = ET.Element("benchmark", {
        "tool": "benchmark_tools.arithexe",
        "displayName": "ArrayRec (integer semantics)" if integer else "ArrayRec",
        "timelimit": f"{args.timelimit}s", "hardtimelimit": f"{args.timelimit}s",
        "walltimelimit": f"{args.walltimelimit}s", "memlimit": args.memory,
        "cpuCores": "1", "threads": str(args.workers),
    })
    run = ET.SubElement(definition, "rundefinition",
                        name=args.strategy + ("-integer" if integer else ""))
    if integer:
        ET.SubElement(run, "option", name="--ignore-bitwidth-constraints")
    group = ET.SubElement(run, "tasks", name=getattr(args, "suite", "array-fpi"))
    for task in tasks:
        ET.SubElement(group, "include").text = task["metadata"]
    properties = {task["property_file"] for task in tasks}
    if len(properties) != 1:
        raise ValueError("The selected tasks must share one reachability property")
    ET.SubElement(group, "propertyfile").text = properties.pop()
    ET.indent(definition)
    return ET.ElementTree(definition)


def read_results(output, tasks=None):
    rows = []
    for path in sorted(output.glob("*.results.*.xml")):
        for run in ET.parse(path).getroot().findall("run"):
            columns = {c.get("title"): c.get("value") for c in run.findall("column")}
            if not columns.get("status"):
                continue
            name = run.get('name')
            identity = Path(name).stem
            if tasks is not None:
                matches = [task for task in tasks if str(Path(task['metadata'])) == name
                           or str(Path(task['metadata'])).endswith('/' + name)]
                if not matches:
                    matches = [task for task in tasks
                               if Path(task['metadata']).name == Path(name).name]
                if len(matches) != 1:
                    raise ValueError(f'ambiguous result task: {name}')
                identity = matches[0]['name']
            rows.append(dict(task=identity,
                             expected=run.get("expectedVerdict"), **columns))
    return rows


def launch_command(command, args, env):
    if args.scope == "none":
        return command
    if args.scope == "user":
        return ["systemd-run", "--user", "--scope", "--slice=benchexec",
                "--property=Delegate=yes", *command]
    # Only the transient service creation is privileged; benchmarks run as us.
    assignments = [f"{key}={env[key]}" for key in (
        "PATH", "PYTHONPATH", "ARITHEXE_MULTIVARIATE_STRATEGY",
        "ARITHEXE_SOLVER_PYTHON", "ARITHEXE_SOLVER_TIMEOUT_MS")]
    properties = ["--property=Delegate=yes", "--property=MemorySwapMax=0"]
    parent = env.get("ARITHEXE_BENCHEXEC_PARENT_UNIT")
    if parent:
        properties += [f"--property=BindsTo={parent}", f"--property=After={parent}"]
    return ["sudo", "-n", "systemd-run", "--wait", "--pipe", "--collect",
            f"--unit=arith-benchexec-{os.getpid()}", f"--uid={os.getuid()}",
            f"--gid={os.getgid()}", *properties,
            f"--working-directory={ROOT}", "/usr/bin/env", *assignments, *command]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("array-fpi", "arrays"), default="array-fpi")
    parser.add_argument("--svcomp", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--task", action="append", help="Exact task stem; repeatable")
    parser.add_argument("--strategy", choices=("tupling", "pivot"), default="tupling")
    parser.add_argument("--semantics", choices=("c", "integer"), default="c",
                        help="integer uses the IR-level integer relaxation without C truth labels")
    parser.add_argument("--timelimit", type=int, default=300, help="CPU seconds per task")
    parser.add_argument("--walltimelimit", type=int, default=360)
    parser.add_argument("--solver-timeout-ms", type=int, default=300000)
    parser.add_argument("--memory", default="3 GB")
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--solver-python", type=Path,
                        default=ROOT.parent / "solver-venv/bin/python")
    parser.add_argument("--scope", choices=("user", "system", "none"), default="user",
                        help="Use system for a transient sudo-created delegated service")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    args.svcomp = args.svcomp or ROOT.parent / ("svcomp26-arrays" if args.suite == "arrays" else "svcomp25")
    args.solver_python = interpreter_path(args.solver_python)
    if min(args.timelimit, args.walltimelimit, args.solver_timeout_ms, args.workers) <= 0:
        parser.error("Resource limits must be positive")
    if args.walltimelimit < args.timelimit:
        parser.error("Wall-time limit must not be shorter than CPU-time limit")
    from benchexec.util import parse_memory_value
    if parse_memory_value(args.memory) <= 0:
        parser.error("Memory limit must be positive")
    tasks = (load_arrays if args.suite == "arrays" else load_tasks)(args.svcomp.resolve())
    if args.task:
        selected = set(args.task)
        missing = selected - {t["name"] for t in tasks}
        if missing:
            parser.error(f"Unknown task names: {sorted(missing)}")
        tasks = [t for t in tasks if t["name"] in selected]
    binary = ROOT / "build/arith_exe"
    if not binary.is_file() or not args.solver_python.is_file():
        parser.error("Build ArithExe and provide its solver Python environment first")
    preflight = subprocess.run(
        [str(args.solver_python), "-c",
         "import json,sys,z3,sympy,solver; "
         "print(json.dumps(dict(executable=sys.executable,prefix=sys.prefix,"
         "z3=z3.get_version_string(),sympy=sympy.__version__)))"],
        cwd=ROOT, text=True, capture_output=True)
    if preflight.returncode:
        parser.error("Solver Python preflight failed:\n" + preflight.stderr)
    solver_runtime = json.loads(preflight.stdout)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = (args.output_dir or ROOT.parent / "results" /
              f"benchexec-{args.suite}-{args.strategy}-{stamp}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    definition = output / (args.suite + ".xml")
    execution_tasks = (integer_tasks(tasks, output / "integer-tasks")
                       if args.semantics == "integer" else tasks)
    make_definition(execution_tasks, args).write(definition, encoding="utf-8", xml_declaration=True)
    env = os.environ.copy()
    env.update(PYTHONPATH=str(ROOT),
               ARITHEXE_MULTIVARIATE_STRATEGY=args.strategy,
               ARITHEXE_SOLVER_PYTHON=str(args.solver_python),
               ARITHEXE_SOLVER_TIMEOUT_MS=str(args.solver_timeout_ms))
    for key in ("LD_PRELOAD", "ARITHEXE_SOLVER_TRACE_DIR"):
        env.pop(key, None)
    benchexec = str(Path(sys.executable).parent / "benchexec")
    command = [benchexec, "--no-container", "--no-compress-results",
               "--results-per-rundefinition", "--tool-directory", str(ROOT),
               "--outputpath", str(output) + os.sep, str(definition)]
    command = launch_command(command, args, env)
    files = sorted(set(git("ls-files", "--cached", "--others", "--exclude-standard",
                           "-z").split("\0")) - {""})
    sources = {p: digest(ROOT / p) for p in files if (ROOT / p).is_file()}
    with tarfile.open(output / "source.tar.gz", "w:gz") as archive:
        for path in sources:
            archive.add(ROOT / path, arcname=path, recursive=False)
    manifest = dict(benchexec_version=version("benchexec"), command=command,
                    suite=args.suite,
                    array_task_definition=(dict(
                        path=str(arrays_definition(args.svcomp)),
                        sha256=digest(arrays_definition(args.svcomp)))
                        if args.suite == "arrays" else None),
                    strategy=args.strategy, git_head=git("rev-parse", "HEAD").strip(),
                    git_branch=git("branch", "--show-current").strip(),
                    git_status=git("status", "--short"),
                    binary_sha256=digest(binary), source_sha256=sources,
                    source_archive_sha256=digest(output / "source.tar.gz"),
                    tasks=tasks, task_count=len(tasks), cpu_seconds=args.timelimit,
                    wall_seconds=args.walltimelimit, memory_bytes=parse_memory_value(args.memory),
                    cores_per_task=1, parallel_tasks=args.workers,
                    solver_timeout_ms=args.solver_timeout_ms,
                    semantics=args.semantics,
                    integer_semantics=("IR-level integer relaxation; bit-width constraints disabled"
                                       if args.semantics == "integer"
                                       else "default fixed-width C; no relaxation flags"),
                    reference_labels_used=args.semantics == "c",
                    execution_tasks=execution_tasks,
                    witness_validation=False, container=False, cgroup_scope=args.scope,
                    solver_python=str(args.solver_python), solver_runtime=solver_runtime,
                    solver_packages=subprocess.check_output(
                        [str(args.solver_python), "-m", "pip", "freeze"], text=True))
    dataset_manifest = args.svcomp / 'download-manifest.json'
    if dataset_manifest.is_file():
        manifest['benchmark_dataset'] = json.loads(dataset_manifest.read_text())
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Results: {output}\nTasks: {len(tasks)}; strategy: {args.strategy}; "
          f"semantics: {args.semantics}", flush=True)
    if args.semantics == "integer":
        print("Fixed-width SV-COMP truth labels are retained in the manifest only; "
              "this run is not scored as C verification.", flush=True)
    if args.prepare_only:
        return 0
    start = time.monotonic()
    with (output / "benchexec.log").open("w") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
            log.flush()
        code = process.wait()
    rows = read_results(output, execution_tasks)
    summary = dict(complete=code == 0 and len(rows) == len(tasks),
                   semantics=args.semantics, reference_labels_used=args.semantics == "c",
                   benchexec_returncode=code, elapsed_seconds=round(time.monotonic() - start, 3),
                   task_count=len(tasks), completed=len(rows),
                   categories=dict(Counter(r.get("category", "missing") for r in rows)),
                   statuses=dict(Counter(r.get("status", "missing") for r in rows)),
                   binary_unchanged=digest(binary) == manifest["binary_sha256"], results=rows)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    xmls = sorted(output.glob("*.results.*.xml"))
    if xmls:
        table_command = [str(Path(sys.executable).parent / "table-generator"),
                         "--outputpath", str(output / "tables"), *map(str, xmls)]
        with (output / "table-generator.log").open("w") as log:
            summary["table_generator_returncode"] = subprocess.run(
                table_command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({k: v for k, v in summary.items() if k != "results"}), flush=True)
    if not summary["complete"] or not summary["binary_unchanged"]:
        return code or 1
    return 2 if summary["categories"].get("wrong", 0) else 0


if __name__ == "__main__":
    raise SystemExit(main())
