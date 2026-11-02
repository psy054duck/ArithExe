#!/usr/bin/env python3
"""Submit the BenchExec runner as a persistent, unprivileged systemd job."""

import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def submission_command(output, unit, options):
    log = output.parent / (output.name + ".service.log")
    return [
        "sudo", "-n", "systemd-run", "--collect", "--service-type=exec",
        f"--unit={unit}", f"--uid={os.getuid()}", f"--gid={os.getgid()}",
        "--property=Delegate=yes", "--property=MemorySwapMax=0",
        f"--property=StandardOutput=append:{log}",
        "--property=StandardError=inherit", f"--working-directory={ROOT}",
        "/usr/bin/env", f"PATH={os.environ.get('PATH', '/usr/bin:/bin')}",
        f"PYTHONPATH={ROOT}", f"ARITHEXE_BENCHEXEC_PARENT_UNIT={unit}.service",
        os.path.abspath(sys.executable),
        str(ROOT / "tools/run_benchexec.py"), "--scope", "system",
        "--output-dir", str(output), *options,
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     epilog="Additional options go to run_benchexec.py")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--unit")
    args, options = parser.parse_known_args()
    if any(option.split("=", 1)[0] == "--scope" for option in options):
        parser.error("The submitted job already has its own delegated cgroup")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("The output directory already exists; choose a new one")
    unit = args.unit or "arrayrec-array-fpi-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", unit):
        parser.error("Unit names may contain only letters, digits, underscores, and hyphens")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(submission_command(output, unit, options), check=True)
    print(f"Submitted: {unit}.service\nResults: {output}\n"
          f"Service log: {output}.service.log\n"
          f"Status: systemctl status {unit}.service\n"
          f"Cancel: sudo systemctl stop {unit}.service")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
