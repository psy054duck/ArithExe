#!/usr/bin/env python3
"""Fingerprint an isolated ArithExe rebuild while retaining the old run config."""
import argparse
import copy
import hashlib
import json
from pathlib import Path


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous-config", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-archive", type=Path, required=True)
    parser.add_argument("--base-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    old = json.loads(args.previous_config.read_text())
    tool = copy.deepcopy(old["arithexe"])
    old_build = Path(tool["executable"]).parent
    build = source / "build"
    tool["executable"] = str(build / "arith_exe")
    tool["executable_sha256"] = sha(build / "arith_exe")
    tool["environment"]["ARITHEXE_SOLVER_WORKER"] = str(build / "solver_worker.py")
    tool["environment"]["ARITHEXE_COARSE_LEMMA_FILE"] = str(build / "lemma.smt2")
    tool["environment"].update(ARITHEXE_COARSE_FALLBACK="1",
        ARITHEXE_COARSE_TIMEOUT_MS="3000", ARITHEXE_COARSE_REFINE_STEPS="512",
        ARITHEXE_COARSE_POLY_EXPR_STRATEGY="auto", ARITHEXE_COARSE_POLY_EXPR_ORDER="1")
    support = {}
    for path in tool.get("support_files_sha256", {}):
        original = Path(path)
        if original.is_relative_to(old_build):
            current = build / original.relative_to(old_build)
        else:
            current = original
        support[str(current)] = sha(current)
    support[str(build / "lemma.smt2")] = sha(build / "lemma.smt2")
    tool["support_files_sha256"] = support
    tool.pop("source_revision", None)
    tool["source_base_revision"] = args.base_revision
    tool["source_archive_sha256"] = sha(args.source_archive)
    tool["source_files_sha256"] = {str(p.relative_to(source)): sha(p)
        for p in source.rglob("*") if p.is_file() and "build" not in p.relative_to(source).parts}
    config = {"dataset": old["dataset"], "arithexe": tool,
              "previous_configuration_sha256": sha(args.previous_config)}
    if args.output.exists():
        raise RuntimeError("Refusing to replace existing rerun configuration")
    args.output.write_text(json.dumps(config, indent=2) + "\n")
    print("Prepared isolated ArithExe rerun configuration:", args.output)


if __name__ == "__main__":
    main()
