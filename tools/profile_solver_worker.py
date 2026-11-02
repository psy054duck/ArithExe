"""Profile each real solver-worker request without changing its protocol."""

import cProfile
import json
import os
from pathlib import Path
import pstats
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import solver_worker

OUTPUT = Path(os.environ["ARITHEXE_REQUEST_PROFILE_DIR"])
OUTPUT.mkdir(parents=True, exist_ok=True)
original_handle = solver_worker.handle_solve
original_solve = solver_worker.solve_str_to_smt2
request_id = None


def handle(request, ind_var_size, recurrence_size):
    global request_id
    request_id = request
    return original_handle(request, ind_var_size, recurrence_size)


def solve(recurrence):
    profile = cProfile.Profile()
    start_cpu, start_wall = time.process_time(), time.monotonic()
    error = None
    (OUTPUT / f"request-{request_id}.rec").write_text(recurrence)
    try:
        return profile.runcall(original_solve, recurrence)
    except Exception as exception:
        error = f"{type(exception).__name__}: {exception}"
        raise
    finally:
        cpu, wall = time.process_time() - start_cpu, time.monotonic() - start_wall
        profile.dump_stats(str(OUTPUT / f"request-{request_id}.pstats"))
        stats = pstats.Stats(profile)
        rows = [dict(file=f, line=line, function=name, calls=values[1],
                     self_seconds=values[2], cumulative_seconds=values[3])
                for (f, line, name), values in stats.stats.items()]
        rows.sort(key=lambda row: row["cumulative_seconds"], reverse=True)
        result = dict(request=request_id, cpu_seconds=cpu, wall_seconds=wall,
                      error=error, top_functions=rows[:100])
        (OUTPUT / f"request-{request_id}.json").write_text(json.dumps(result, indent=2))


solver_worker.handle_solve = handle
solver_worker.solve_str_to_smt2 = solve

if __name__ == "__main__":
    solver_worker.serve()
