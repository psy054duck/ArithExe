#!/usr/bin/env python3
"""Sequential, resumable Linux comparison using BenchExec's cgroup executor.

No original SV-COMP label is treated as validated ground truth. The dataset has
been transformed, and ArithExe/ICRA use arithmetic abstractions.
"""
import argparse
from collections import Counter
import csv
import hashlib
import importlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import time

from benchexec.runexecutor import RunExecutor
from benchexec.tools.template import BaseTool2
import benchexec


def sha(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare_cgroups():
    # Ubuntu 26.04/systemd 259 delegates controllers but does not activate
    # memory for DelegateSubgroup=manager. Enable it only in this experiment's
    # explicitly delegated service, never in arbitrary host/user cgroups.
    unified = next(line.split(":", 2)[2] for line in Path("/proc/self/cgroup").read_text().splitlines()
                   if line.startswith("0::"))
    current = Path("/sys/fs/cgroup") / unified.lstrip("/")
    if current.name == "manager" and current.parent.name.startswith("path-expression-") and current.parent.name.endswith(".service"):
        available = (current.parent / "cgroup.controllers").read_text().split()
        if not {"cpu", "memory"}.issubset(available):
            raise RuntimeError("CPU/memory controllers unavailable in delegated experiment service")
        (current.parent / "cgroup.subtree_control").write_text("+cpu +memory")


def save(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, default=str) + "\n")
    temporary.replace(path)


def icra_model(model):
    wide = model == "LP64"
    return " ".join([
        "underscore_name=false", "short=2,2", "bool=1,1", "int=4,4",
        "long=8,8" if wide else "long=4,4", "long_long=8,8" if wide else "long_long=8,4",
        "pointer=8,8" if wide else "pointer=4,4", "alignof_enum=4", "float=4,4",
        "double=8,8" if wide else "double=8,4", "long_double=16,16" if wide else "long_double=12,4",
        "void=1", "fun=1,1", "alignof_string=1", "max_alignment=16",
        "size_t=unsigned_long" if wide else "size_t=unsigned_int", "wchar_t=int",
        "char_signed=true", "const_string_literals=false", "big_endian=false",
        "__thread_is_keyword=true", "__builtin_va_list=true"])


def classify_icra(text):
    checks = re.findall(r"Is (?:not )?SAT! \(Assertion on line (-?\d+) (PASSED|FAILED)\)", text)
    start = text.find("Assertion Checking at Error Points")
    end = text.find("Bounds on Variables", start + 1) if start >= 0 else -1
    count = len(re.findall("Checking assertion at vertex", text[start:end])) if end >= 0 else 0
    if checks and count == len(checks) and all(v == "PASSED" for _, v in checks):
        return "TRUE"
    return "UNKNOWN"  # FAILED is an unproved abstraction, not a counterexample.


def classify(name, text, measurement, module=None, command=None):
    reason = measurement.get("terminationreason", "")
    if reason in {"walltime", "cputime", "cputime-soft"}:
        return "TIMEOUT", reason
    if reason == "memory":
        return "MEMORY_LIMIT", reason
    code = measurement["exitcode"]
    if code.signal:
        return "CRASH", "signal " + str(code.signal)
    if code.value:
        return "TOOL_ERROR", "exit " + str(code.value)
    if reason:
        return "TOOL_ERROR", reason
    if name == "icra":
        result = classify_icra(text)
    elif name == "arithexe":
        matches = re.findall(r"^((?:TRUE|FALSE)(?:\([^\n]*\))?|UNKNOWN)\s*$", text, re.M)
        result = matches[-1] if matches else "UNKNOWN"
    else:
        run = BaseTool2.Run(command, code, BaseTool2.RunOutput(text.splitlines(True)), None)
        result = module.determine_result(run)
    if result.lower() == "true" or result.lower().startswith("true("):
        return "TRUE", result
    if result.lower().startswith("false"):
        return "FALSE", result
    if result.lower().startswith("error"):
        return "TOOL_ERROR", result
    return "UNKNOWN", result


def export(output, rows, total):
    fields = ["tool", "task", "component", "data_model", "integer_only", "changed",
              "original_expected_verdict", "verdict", "reason", "nominal_label_match",
              "walltime", "cputime", "memory", "exit_value", "exit_signal", "log"]
    temporary = output / "results.csv.tmp"
    with temporary.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(output / "results.csv")
    summary = {"completed": len(rows), "planned": total, "tools": {},
               "caveat": "Nominal label matches are not validated correctness; inputs are transformed and arithmetic models differ."}
    for name in sorted({r["tool"] for r in rows}):
        group = [r for r in rows if r["tool"] == name]
        summary["tools"][name] = dict(completed=len(group), verdicts=dict(Counter(r["verdict"] for r in group)),
                                     nominal_label_matches=sum(r["nominal_label_match"] is True for r in group),
                                     nominal_label_mismatches=sum(r["nominal_label_match"] is False for r in group),
                                     wall_seconds=sum(r.get("walltime", 0) for r in group))
    save(output / "summary.json", summary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tools", nargs="+", choices=["arithexe", "icra", "veriabs", "automizer"],
                        default=["arithexe", "icra", "veriabs", "automizer"])
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--memory-mib", type=int, default=1024)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if sys.platform != "linux" or args.timeout <= 0 or args.memory_mib <= 0:
        parser.error("Linux and positive resource limits required")
    config = json.loads(args.config.read_text())
    dataset = Path(config["dataset"]).resolve()
    records = [r for r in json.loads((dataset / "transformations.json").read_text()) if r["status"] == "included"]
    records.sort(key=lambda r: r["task"])
    if args.limit:
        records = records[:args.limit]
    for r in records:
        if len(r["input_files"]) != 1 or sha(dataset / "tasks" / r["input_files"][0]) != r["processed_sha256"]:
            raise RuntimeError("Dataset identity mismatch: " + r["task"])
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    identity = dict(config=config, tools=args.tools, timeout=args.timeout, memory_mib=args.memory_mib,
                    tasks=[(r["task"], r["processed_sha256"]) for r in records],
                    runner_sha256=sha(__file__), benchexec_version=benchexec.__version__,
                    architecture=platform.machine(), platform=platform.platform(),
                    resource_model="BenchExec cgroups; 30s wall limit (not SV-COMP CPU budget); sequential; 1GiB includes descendants")
    identity_path = output / "configuration.json"
    if identity_path.exists():
        if not args.resume or json.loads(identity_path.read_text()) != identity:
            raise RuntimeError("Existing run requires --resume and identical configuration")
    else:
        save(identity_path, identity)
    rows = []
    journal = output / "results.jsonl"
    if journal.exists():
        for line in journal.read_text().splitlines():
            if line:
                rows.append(json.loads(line))
    done = {(r["tool"], r["task"]) for r in rows}
    prepare_cgroups()
    executor = RunExecutor(use_namespaces=False)
    for controller in (executor.cgroups.MEMORY, executor.cgroups.CPU, executor.cgroups.FREEZE):
        if controller not in executor.cgroups:
            raise RuntimeError("Required cgroup controller unavailable: " + controller)
    interrupted = False
    def stop(signum, frame):
        nonlocal interrupted
        interrupted = True
        executor.stop()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    property_file = str(dataset / "tasks/c/properties/unreach-call.prp")
    modules = {n: importlib.import_module("benchexec.tools." + ("ultimateautomizer" if n == "automizer" else n)).Tool()
               for n in args.tools if n in {"automizer", "veriabs"}}
    # Cached version detection occurs before measured verification, not inside each task.
    versions = {n: m.version(config[n]["executable"]) for n, m in modules.items()}
    save(output / "tool-versions.json", versions)
    total = len(records) * len(args.tools)
    export(output, rows, total)
    for name in args.tools:
        tool = config[name]
        if not Path(tool["executable"]).is_file():
            raise RuntimeError("Missing tool: " + name)
        for record in records:
            if interrupted:
                return 130
            if (name, record["task"]) in done:
                continue
            number = next(i for i, r in enumerate(records) if r["task"] == record["task"])
            directory = output / "runs" / name / f"{number:04d}-{Path(record['task']).stem}"
            directory.mkdir(parents=True, exist_ok=True)
            source = dataset / "tasks" / record["input_files"][0]
            environment = dict(tool.get("environment", {}))
            model = record["data_model"]
            if name == "arithexe":
                command = [tool["executable"], "--data-model=" + model,
                           "--property-file=" + property_file, "--witness=" + str(directory / "witness.yml"),
                           "--ignore-bitwidth-constraints", str(source)]
            elif name == "icra":
                prepared = directory / "icra-input.c"
                prepared.write_bytes(source.read_bytes())
                environment["CIL_MACHINE"] = icra_model(model)
                command = [tool["executable"], "--newton-verbosity", "0", "-cra-split-loops", "-loadpath", tool["loadpath"],
                           "-cflags", "-m32" if model == "ILP32" else "-m64", str(prepared)]
            else:
                task = BaseTool2.Task([str(source)], None, property_file, {"language": "C", "data_model": model})
                limits = BaseTool2.ResourceLimits(cputime=None, cputime_hard=None, walltime=args.timeout,
                                                  memory=args.memory_mib * 1024**2, cpu_cores=None)
                command = modules[name].cmdline(tool["executable"], list(tool["options"]), task, limits)
            save(directory / "command.json", dict(command=command, environment=environment, input_sha256=sha(source)))
            log = directory / "output.log"
            measurement = executor.execute_run(command, str(log), walltimelimit=args.timeout,
                                               memlimit=args.memory_mib * 1024**2, workingDir=str(directory),
                                               environments={"newEnv": environment}, maxLogfileSize=16 * 1024**2,
                                               write_header=False)
            verdict, reason = classify(name, log.read_text(errors="replace"), measurement, modules.get(name), command)
            row = {k: record[k] for k in ["task", "component", "data_model", "integer_only", "changed", "original_expected_verdict"]}
            code = measurement["exitcode"]
            row.update(tool=name, verdict=verdict, reason=reason, nominal_label_match=None,
                       exit_value=code.value, exit_signal=code.signal, log=str(log.relative_to(output)))
            for field in ["walltime", "cputime", "memory"]:
                row[field] = measurement.get(field, 0)
            if verdict in {"TRUE", "FALSE"}:
                row["nominal_label_match"] = (verdict == "TRUE") == (str(record["original_expected_verdict"]).lower() == "true")
            save(directory / "measurement.json", measurement)
            with journal.open("a") as stream:
                stream.write(json.dumps(row) + "\n")
                stream.flush()
                os.fsync(stream.fileno())
            rows.append(row)
            export(output, rows, total)
            print(f"{len(rows)}/{total} {name} {record['task']}: {verdict} ({row['walltime']:.2f}s)", flush=True)
    save(output / "completed.json", {"time": time.time(), "runs": len(rows), "planned": total})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
