"""Retest the exact 21-task ICRA-only set, locally and sequentially."""
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
baseline = repo / 'experiments/diagnostics/comparison-no-nla-scaling-20261006'
a = {r['task']:r for r in map(json.loads,(baseline/'arithexe/results.jsonl').read_text().splitlines())}
b = {(r['tool'],r['task']):r for r in map(json.loads,(baseline/'baseline/results.jsonl').read_text().splitlines())}
tasks = sorted(t for t in a if a[t]['verdict']!='TRUE' and any(b[n,t]['verdict']=='TRUE' for n in ('icra_split','icra_refine')))
assert len(tasks)==21
dataset = repo.parent/'svcomp2026-single-assertions'
records = {r['task']:r for r in json.loads((dataset/'transformations.json').read_text()) if r['status']=='included'}
output = repo/'experiments/diagnostics'/datetime.datetime.now().strftime('icra-only-21-%Y%m%d-%H%M%S')
output.mkdir()
env = {k:v for k,v in os.environ.items() if not k.startswith('ARITHEXE_')}
env.update(ARITHEXE_SOLVER_PYTHON='/opt/miniconda3/envs/veri/bin/python',
           ARITHEXE_SOLVER_WORKER=str(repo/'build/solver_worker.py'),
           ARITHEXE_COARSE_LEMMA_FILE=str(repo/'build/lemma.smt2'))
source_files = [repo/'lib/PathAccelerator.cpp', repo/'lib/AInstruction.cpp', repo/'lib/engine.cpp', repo/'solver.py']
source_files += sorted((repo/'build/rec_solver').rglob('*.py'))
(output/'configuration.json').write_text(json.dumps(dict(tasks=tasks,timeout_seconds=30,jobs=1,
    scheduling='defaults',mode='integer-relaxed',platform='local macOS ARM; not a server timing comparison',
    environment={k:v for k,v in env.items() if k.startswith('ARITHEXE_')},
    binary_sha256=hashlib.sha256((repo/'build/arith_exe').read_bytes()).hexdigest(),
    source_files_sha256={str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}),indent=2)+'\n')
print(output,flush=True)
rows=[]
for task in tasks:
    record=records[task]
    source=dataset/'tasks'/record['input_files'][0]
    log=output/(Path(task).stem+'.log')
    command=[str(repo/'build/arith_exe'),'--verbose','--ignore-bitwidth-constraints','--no-witness',
             '--data-model='+a[task]['data_model'],str(source)]
    start=time.monotonic()
    with log.open('w') as stream:
        p=subprocess.Popen(command,cwd=repo,env=env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        timeout=False
        try:p.wait(timeout=30)
        except subprocess.TimeoutExpired:
            timeout=True
            os.killpg(p.pid,signal.SIGKILL)
            p.wait()
    text=log.read_text(errors='replace')
    matches=re.findall(r'^(TRUE\(integer-relaxed\)|FALSE\(integer-relaxed\)|UNKNOWN)\s*$',text,re.M)
    row=dict(task=task,verdict='TIMEOUT' if timeout else matches[-1] if matches else 'ERROR',
             seconds=time.monotonic()-start,exit_code=p.returncode,
             source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),command=command,
             worker_errors=re.findall(r'Error solving recurrence through worker:.*',text))
    rows.append(row)
    with (output/'results.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
    print(len(rows),'/21',task,row['verdict'],round(row['seconds'],2),flush=True)
    solved=sum(r['verdict']=='TRUE(integer-relaxed)' for r in rows)
    (output/'progress.json').write_text(json.dumps(dict(completed=len(rows),planned=21,verified=solved,
                                                     not_verified=len(rows)-solved),indent=2)+'\n')
(output/'completed.json').write_text(json.dumps(dict(completed=21,verified=solved,not_verified=21-solved),indent=2)+'\n')
lines=['# Latest local retest of 21 ICRA-only tasks','',
       '30 seconds/task, sequential, default scheduling, integer-relaxed. Local ARM timings are not directly comparable to the historical x86 server runs.','',
       f'Verified: {solved}/21. Not verified within the limit: {21-solved}.','',
       '| Task | Verdict | Seconds |','|---|---|---:|']
lines += [f"| {r['task']} | {r['verdict']} | {r['seconds']:.2f} |" for r in rows]
(output/'SUMMARY.md').write_text('\n'.join(lines)+'\n')
