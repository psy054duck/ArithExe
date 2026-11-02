#!/usr/bin/env python3
"""Inspect the certified tuple independently of scalar closed-form solving."""

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver import parse_file
from rec_solver.core.tupling import TuplingOptions, TuplingUnsupported, tuple_recurrence


def describe(result):
    rec = result.recurrence
    return {
        "strategy": "guarded-unfold-fold-tupling",
        "signature": str(rec.func_sig),
        "components": [[str(a) for a in t] for t in result.components],
        "ranking": str(result.ranking),
        "rounds": result.rounds,
        "proof_obligations": result.proof_obligations,
        "base_cases": [{"guard": str(c.condition), "results": list(map(str, c.op))}
                       for c in rec.get_base_cases()],
        "recursive_cases": [{"guard": str(c.condition),
                             "call": str(next(iter(c.recursive_calls.values()))),
                             "result_names": list(map(str, c.recursive_calls)),
                             "results": list(map(str, c.op))}
                            for c in rec.get_rec_cases()],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recurrence", type=Path)
    parser.add_argument("--max-components", type=int, default=8)
    parser.add_argument("--max-rounds", type=int, default=12)
    parser.add_argument("--max-branches", type=int, default=128)
    parser.add_argument("--solver-timeout-ms", type=int, default=1000)
    args = parser.parse_args()
    try:
        options = TuplingOptions(args.max_components, args.max_rounds,
                                 args.max_branches, args.solver_timeout_ms)
        result = tuple_recurrence(parse_file(str(args.recurrence)), options)
        print(json.dumps(describe(result), indent=2))
    except (TuplingUnsupported, ValueError) as error:
        print(json.dumps({"status": "unsupported", "reason": str(error)}))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
