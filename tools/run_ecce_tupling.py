#!/usr/bin/env python3
"""Run the unmodified ECCE baseline on a Prolog source, preserving artifacts."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--ecce", type=Path, required=True)
    parser.add_argument("--goal", default="fib(N,F)")
    parser.add_argument("--config", choices=["default", "fast", "classic", "minimal",
                                           "term", "deforest", "mixtus"], default="default")
    parser.add_argument("--msv", action="store_true")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    residual = args.output_dir / "residual.pl"
    source = args.source.resolve()
    executable = args.ecce.resolve()
    command = [str(executable)]
    if args.config != "default":
        command += ["-config", args.config]
    if args.msv:
        command += ["-pp_msv", "yes"]
    command += ["-pe", args.goal, "-o", str(residual.resolve()), str(source)]
    metadata = {"command": command, "configuration": args.config,
                "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                "note": "A residual program is not a certified array summary."}
    try:
        process = subprocess.run(command, capture_output=True, text=True,
                                 timeout=args.timeout, check=False)
        metadata.update(status="finished", returncode=process.returncode)
        (args.output_dir / "stdout.log").write_text(process.stdout)
        (args.output_dir / "stderr.log").write_text(process.stderr)
    except subprocess.TimeoutExpired:
        metadata.update(status="timeout", returncode=124)
    (args.output_dir / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))
    return metadata["returncode"]


if __name__ == "__main__":
    raise SystemExit(main())
