#!/usr/bin/env python3
"""Prepare an isolated three-configuration memory rerun from a finished baseline."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-results", type=Path, required=True)
    parser.add_argument("--run-dir", type=Path, required=True)
    args = parser.parse_args()
    base = args.base_results.resolve()
    run = args.run_dir.resolve()
    completed = json.loads((base / "completed.json").read_text())
    if completed.get("runs") != completed.get("planned") or not completed.get("tool_hashes_verified"):
        raise RuntimeError("Baseline is not complete and fingerprint-verified")
    identity = json.loads((base / "configuration.json").read_text())
    names = ["icra_split", "icra_refine", "automizer"]
    config = {"dataset": identity["config"]["dataset"]}
    config.update({name: identity["config"][name] for name in names})
    if config["icra_split"].get("options") != ["-cra-split-loops"]:
        raise RuntimeError("Expected split-loops-only configuration")
    if config["icra_refine"].get("options") != ["-cra-refine"]:
        raise RuntimeError("Expected refine-only configuration")
    run.mkdir(parents=True, exist_ok=True)
    for name, data in [("configuration.json", config), ("preparation.json", {
        "baseline_results": str(base),
        "baseline_identity_sha256": hashlib.sha256((base / "configuration.json").read_bytes()).hexdigest(),
        "tools": names, "timeout": identity["timeout"],
        "memory_mib": identity["memory_mib"], "dataset": config["dataset"],
        "task_count": len(identity["tasks"]),
        "memory_metric": "BenchExec cgroup memory.peak in bytes, entire process tree including charged cache",
        "policy": "New results and immutable runner copy; unchanged tools, dataset, options and resource limits."
    })]:
        with (run / name).open("x") as stream:
            json.dump(data, stream, indent=2)
            stream.write("\n")
    print(json.dumps(json.loads((run / "preparation.json").read_text()), indent=2))


if __name__ == "__main__":
    main()
