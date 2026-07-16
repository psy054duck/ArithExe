#!/usr/bin/env python3
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent

BASES = [
    "bcf_01_square_invariant",
    "bcf_06_cubic_invariant",
    "bcf_07_mixed_quadratic_invariant",
    "bcf_09_weighted_linear_phi",
    "bcf_10_quartic_invariant",
]

BOUNDS = [
    ("b20", "20"),
    ("b1000", "1000"),
    ("bmax", "2147483647"),
]


def rewrite_bound(source, bound):
    pattern = r"assume_abort_if_not\(n <= [^)]+\);"
    replacement = f"assume_abort_if_not(n <= {bound});"
    updated, count = re.subn(pattern, replacement, source, count=1)
    if count != 1:
        raise RuntimeError("could not find the upper-bound assumption")
    return updated


def write_yml(path, input_file):
    path.write_text(
        f"""format_version: '2.0'

input_files: '{input_file}'

properties:
  - property_file: ../properties/unreach-call.prp
    expected_verdict: true

options:
  language: C
  data_model: ILP32
"""
    )


def main():
    for base in BASES:
        source = (ROOT / f"{base}.c").read_text()
        for suffix, bound in BOUNDS:
            out_name = f"{base}_{suffix}.c"
            out_path = ROOT / out_name
            out_path.write_text(rewrite_bound(source, bound))
            write_yml(ROOT / f"{base}_{suffix}.yml", out_name)
            print(out_name)


if __name__ == "__main__":
    main()
