"""Crash smoke audit; short timeouts are not proof-coverage measurements."""
import datetime
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
old = ROOT/'experiments/diagnostics/oopsla23-verifier-20261006-192400/results.jsonl'
cases = [r for r in map(json.loads,old.read_text().splitlines())
         if r['mode']=='oopsla23' and (r['status']=='CRASH' or r['task'] in (
             'c/loops/for_bounded_loop1.yml', 'c/nla-digbench/cohencu-ll__assert03.yml'))]
output = ROOT/'experiments/diagnostics'/datetime.datetime.now().strftime('oopsla23-debug-%Y%m%d-%H%M%S')
output.mkdir()
print(output, flush=True)
env = dict(os.environ, ARITHEXE_PERIODIC_STRATEGY='oopsla23', ARITHEXE_COARSE_FALLBACK='0',
           ARITHEXE_SOLVER_PYTHON='/opt/miniconda3/envs/veri/bin/python',
           ARITHEXE_SOLVER_WORKER=str(ROOT/'build/solver_worker.py'))
rows = []
for case in cases:
    source = case['command'][-1]
    log = output/(Path(source).stem+'.log')
    start = time.monotonic()
    with log.open('w') as stream:
        process = subprocess.Popen([str(ROOT/'build/arith_exe'),'--ignore-bitwidth-constraints',
            '--no-witness', source], cwd=ROOT, env=env, stdout=stream,
            stderr=subprocess.STDOUT, start_new_session=True)
        timeout = False
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            timeout = True
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    text = log.read_text(errors='replace')
    verdicts = re.findall(r'^(TRUE\(integer-relaxed\)|FALSE\(integer-relaxed\)|UNKNOWN)\s*$',text,re.M)
    row = dict(task=case['task'], old_status=case['status'],
        status='TIMEOUT' if timeout else verdicts[-1] if verdicts else 'CRASH' if process.returncode<0 else 'ERROR',
        exit_code=process.returncode, seconds=time.monotonic()-start,
        explicitly_rejected='[oopsla23] Rejecting' in text,
        log=str(log), timeout_seconds=3)
    rows.append(row)
    with (output/'results.jsonl').open('a') as stream:
        stream.write(json.dumps(row)+'\n')
    print(len(rows), '/',len(cases), row['task'], row['status'],flush=True)
(output/'completed.json').write_text(json.dumps(dict(total=len(rows),
    crashes=sum(r['status']=='CRASH' for r in rows),
    scope='3-second crash smoke audit, not verification benchmark coverage'),indent=2))
