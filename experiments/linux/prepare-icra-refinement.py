#!/usr/bin/env python3
"""Prepare an isolated refinement run using the fingerprinted ICRA baseline."""
import argparse
import copy
import hashlib
import json
from pathlib import Path


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous-config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    old = json.loads(args.previous_config.read_text())
    tool = copy.deepcopy(old["icra"])
    if sha(tool["executable"]) != tool["executable_sha256"]:
        raise RuntimeError("ICRA executable changed since the baseline")
    for path, expected in tool.get("support_files_sha256", {}).items():
        if sha(path) != expected:
            raise RuntimeError("ICRA support file changed since the baseline: " + path)
    records = json.loads((Path(old["dataset"]) / "transformations.json").read_text())
    included = [record for record in records if record["status"] == "included"]
    if not included or any(not record["integer_only"] for record in included):
        raise RuntimeError("Expected the complete retained integer-only dataset")
    tool["options"] = ["-cra-split-loops", "-cra-refine"]
    config = {"dataset": old["dataset"], "icra": tool,
              "previous_configuration_sha256": sha(args.previous_config),
              "experiment": "ICRA restricted path refinement plus baseline loop splitting",
              "retained_integer_tasks": len(included)}
    # Exclusive create: never replace baseline or an existing rerun config.
    with args.output.open("x") as stream:
        json.dump(config, stream, indent=2)
        stream.write("\n")
    print(f"Prepared ICRA refinement configuration for {len(included)} integer tasks: {args.output}")


if __name__ == "__main__":
    main()
