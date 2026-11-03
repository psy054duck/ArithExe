#!/usr/bin/env python3
"""Compare exact task identities and verdicts, including incomplete runs."""

import argparse
import bz2
from collections import Counter
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def read(directory):
    files = list(directory.glob("*.results.*.xml")) + list(directory.glob("*.results.*.xml.bz2"))
    if len(files) != 1:
        raise ValueError(f"Expected exactly one result XML in {directory}")
    path = files[0]
    root = ET.fromstring(bz2.decompress(path.read_bytes())) if path.suffix == ".bz2" else ET.parse(path).getroot()
    rows = {}
    for run in root.findall("run"):
        name = Path(run.get("name")).stem
        if name in rows:
            raise ValueError(f"Duplicate task identity: {name}")
        rows[name] = {c.get("title"): c.get("value") for c in run.findall("column")}
    return rows


def compare(previous, current):
    good = {"true", "false(unreach-call)"}
    completed = {k: v for k, v in current.items() if v.get("status")}
    shared = set(previous) & set(completed)
    def changes(predicate):
        return [dict(task=k, before=previous[k].get("status"), after=completed[k]["status"])
                for k in sorted(shared) if predicate(previous[k].get("status"), completed[k]["status"])]
    return dict(total=len(current), completed=len(completed),
                statuses=dict(Counter(v["status"] for v in completed.values())),
                missing_tasks=sorted(set(previous)-set(current)),
                extra_tasks=sorted(set(current)-set(previous)),
                losses=changes(lambda a, b: a in good and b not in good),
                flips=changes(lambda a, b: a in good and b in good and a != b),
                gains=changes(lambda a, b: a not in good and b in good))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous", type=Path, required=True)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = compare(read(args.previous), read(args.current))
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")


if __name__ == "__main__":
    main()
