import contextlib
import os
import shutil
import subprocess
import sys
import traceback
from pathlib import Path


def candidate_solver_pythons():
    seen = set()

    def add(command):
        if not command or not command[0]:
            return
        key = tuple(command)
        if key in seen:
            return
        seen.add(key)
        yield command

    yield from add([os.environ.get("ARITHEXE_SOLVER_PYTHON", "")])

    conda_prefix = os.environ.get("CONDA_PREFIX")
    if conda_prefix:
        yield from add([str(Path(conda_prefix) / "bin" / "python")])

    conda_env = os.environ.get("ARITHEXE_SOLVER_CONDA_ENV", "veri")
    conda_exe = os.environ.get("CONDA_EXE") or shutil.which("conda")
    if not conda_exe:
        return

    try:
        conda_base = subprocess.check_output(
            [conda_exe, "info", "--base"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=10,
        ).strip()
    except Exception:
        conda_base = ""

    if conda_base:
        yield from add([str(Path(conda_base) / "envs" / conda_env / "bin" / "python")])

    yield from add([conda_exe, "run", "-n", conda_env, "python"])


def same_python(command):
    if len(command) != 1:
        return False
    try:
        return Path(command[0]).resolve() == Path(sys.executable).resolve()
    except OSError:
        return False


def can_import_solver_dependencies(command):
    try:
        subprocess.run(
            command + ["-c", "import z3, fire, sympy"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
            timeout=10,
        )
        return True
    except Exception:
        return False


def reexec_with_solver_python():
    if os.environ.get("ARITHEXE_SOLVER_REEXECED") == "1":
        return

    env = os.environ.copy()
    env["ARITHEXE_SOLVER_REEXECED"] = "1"
    worker_path = str(Path(__file__).resolve())
    for command in candidate_solver_pythons():
        if same_python(command):
            continue
        if can_import_solver_dependencies(command):
            os.execvpe(command[0], command + ["-u", worker_path], env)


try:
    from solver import solve_str_to_smt2
except ModuleNotFoundError as error:
    if error.name in {"z3", "fire", "sympy"}:
        reexec_with_solver_python()
    raise


def read_exact(size):
    chunks = []
    remaining = size
    while remaining:
        chunk = sys.stdin.buffer.read(remaining)
        if not chunk:
            raise EOFError("unexpected EOF while reading solver request")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def write_response(status, request_id, payload):
    data = payload.encode("utf-8", errors="replace")
    header = f"{status} {request_id} {len(data)}\n".encode("ascii")
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def handle_solve(request_id, ind_var_size, recurrence_size):
    # The C++ side sends the induction variable for protocol stability. The
    # current solver API infers it from the recurrence text, matching solver.py.
    read_exact(ind_var_size)
    recurrence = read_exact(recurrence_size).decode("utf-8")
    try:
        with contextlib.redirect_stdout(sys.stderr):
            smt2 = solve_str_to_smt2(recurrence)
        write_response("OK", request_id, smt2)
    except Exception:
        write_response("ERR", request_id, traceback.format_exc())


def serve():
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            break
        parts = line.decode("ascii").strip().split()
        if not parts:
            continue
        if parts[0] == "QUIT":
            break
        if len(parts) != 4 or parts[0] != "SOLVE":
            write_response("ERR", parts[1] if len(parts) > 1 else "0", "invalid request header")
            continue
        _, request_id, ind_var_size, recurrence_size = parts
        try:
            handle_solve(request_id, int(ind_var_size), int(recurrence_size))
        except Exception:
            write_response("ERR", request_id, traceback.format_exc())


if __name__ == "__main__":
    serve()
