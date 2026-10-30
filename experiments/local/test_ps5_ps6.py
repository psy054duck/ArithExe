"""Sequential local retest of unscaled ps5/ps6 single-assertion tasks."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

repo = Path(__file__).resolve().parents[2]
tasks = sorted((repo.parent / 'svcomp2026-single-assertions/tasks/c/nla-digbench').glob('ps[56]-ll__assert*.c'))
assert len(tasks) == 4
output = repo / 'experiments/diagnostics' / datetime.datetime.now().strftime('ps5-ps6-%Y%m%d-%H%M%S')
output.mkdir()
env = {k:v for k,v in os.environ.items() if not k.startswith('ARITHEXE_')}
env.update(ARITHEXE_SOLVER_PYTHON='/opt/miniconda3/envs/veri/bin/python',
           ARITHEXE_SOLVER_WORKER=str(repo / 'build/solver_worker.py'),
           ARITHEXE_COARSE_LEMMA_FILE=str(repo / 'build/lemma.smt2'))
print(output, flush=True)
results = []
for task in tasks:
    command = [str(repo / 'build/arith_exe'), '--verbose', '--ignore-bitwidth-constraints',
               '--no-witness', '--data-model=ILP32', str(task)]
    start = time.monotonic()
    log = output / (task.stem + '.log')
    with log.open('w') as stream:
        process = subprocess.Popen(command, cwd=repo, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    elapsed = time.monotonic() - start
    text = log.read_text()
    verdicts = re.findall(r'^(TRUE\(integer-relaxed\)|FALSE\(integer-relaxed\)|UNKNOWN)\s*$', text, re.M)
    row = dict(task=task.name, seconds=elapsed, exit_code=process.returncode,
               verdict='TIMEOUT' if timed_out else verdicts[-1] if verdicts else 'ERROR',
               input_sha256=hashlib.sha256(task.read_bytes()).hexdigest(), command=command,
               worker_errors=re.findall(r'Error solving recurrence through worker:.*',text))
    results.append(row)
    (output / 'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(row['task'], row['verdict'], round(elapsed,3), flush=True)
(output / 'configuration.json').write_text(json.dumps(dict(timeout=180, scheduling='defaults',
    mode='integer-relaxed', jobs=1, environment={k:v for k,v in env.items() if k.startswith('ARITHEXE_')},
    binary_sha256=hashlib.sha256((repo / 'build/arith_exe').read_bytes()).hexdigest()),indent=2)+'\n')
