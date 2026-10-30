"""Test-only worker: reject precise SOLVE, keep genuine COARSE synthesis."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import solver_worker


def reject_precise(_recurrence):
    raise RuntimeError("test-only precise solver rejection")


solver_worker.solve_str_to_smt2 = reject_precise
solver_worker.serve()
