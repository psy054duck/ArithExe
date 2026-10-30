"""Deliberately invalid solver used only by coarse-certificate regressions."""
import os
import re
import signal
import sys

while True:
    header = sys.stdin.buffer.readline().decode().split()
    if not header or header[0] == "QUIT":
        break
    mode, request_id, n_size, rec_size = header[:4]
    n = sys.stdin.buffer.read(int(n_size)).decode()
    recurrence = sys.stdin.buffer.read(int(rec_size)).decode()
    if os.environ.get("ARITHEXE_TEST_COARSE_WORKER_STALL") == "1":
        signal.pause()  # The timeout must terminate and reap this worker.
    match = re.search(r"(\w+)\(0\)\s*=\s*(\w+)\s*;", recurrence)
    variable, initial = match.groups()
    # Valid for at most one branch: an individual x or y does not increment
    # under both maps. The base case holds, but joint step validation must fail.
    smt2 = (f"(declare-const {variable} Int)\n"
            f"(declare-const {initial} Int)\n"
            f"(declare-const {n} Int)\n"
            f"(assert (= {variable} (+ {initial} {n})))\n").encode()
    sys.stdout.buffer.write(f"OK {request_id} {len(smt2)}\n".encode() + smt2)
    sys.stdout.buffer.flush()
