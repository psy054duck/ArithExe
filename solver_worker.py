import contextlib
import sys
import traceback

from solver import solve_str_to_smt2


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
