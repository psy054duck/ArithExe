"""Paired isolated sampling solvers on shipped recurrence examples (macOS)."""
import argparse
import collections
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def worker(path, mode):
    def emit(value):
        value['peak_mib'] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 2**20
        print(json.dumps(value))
    sys.path.insert(0, str(ROOT))
    import solver  # establishes recurrence-package imports
    from rec_solver.rec_parser import parse_str
    from rec_solver.core.recurrence import LoopRecurrence
    from rec_solver.core.ultimately_periodic import solve_ultimately_periodic_symbolic
    from rec_solver.core.solver import _direct_function_closed_form_is_inductive
    rec = parse_str(Path(path).read_text())
    if not isinstance(rec, LoopRecurrence):
        emit(dict(status='OUT_OF_SCOPE'))
        return
    start = time.monotonic()
    try:
        result = solve_ultimately_periodic_symbolic(
            rec, enable_bounded_cfinite=False, pattern_strategy=mode)
        elapsed = time.monotonic() - start
        # Check the result independently against initial values and transitions.
        certified = _direct_function_closed_form_is_inductive(rec, result)
        emit(dict(status='CERTIFIED' if certified else 'UNCERTIFIED',
                  solve_seconds=elapsed, formula=str(result)))
    except Exception as error:
        emit(dict(status='FAILED', seconds=time.monotonic()-start,
                  error=type(error).__name__ + ': ' + str(error)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', nargs=2)
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--cases', nargs='+', help='Optional shipped example filenames')
    args = parser.parse_args()
    if args.worker:
        worker(*args.worker)
        return
    output = ROOT/'experiments/diagnostics'/datetime.datetime.now().strftime('periodic-sampling-%Y%m%d-%H%M%S')
    output.mkdir()
    cases = sorted((ROOT/'rec_solver/examples').glob('test*.txt'))
    if args.cases:
        cases = [p for p in cases if p.name in args.cases]
        if len(cases) != len(set(args.cases)):
            parser.error('Unknown or duplicate case filename')
    sources = sorted((ROOT/'rec_solver/rec_solver/core').glob('*.py'))
    (output/'configuration.json').write_text(json.dumps(dict(
        platform=platform.platform(), python=sys.executable, timeout=args.timeout,
        repeats=args.repeats, jobs=1, cases=[str(p) for p in cases],
        scope='isolated recurrence sampling, no C frontend or online acceleration; bounded-C-finite disabled in both',
        memory='macOS getrusage(RUSAGE_SELF) peak RSS in bytes; single Python solver process; unavailable on timeout',
        hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}), indent=2))
    print(output, flush=True)
    rows = []
    for repeat in range(args.repeats):
        for index, case in enumerate(cases):
            modes = ['oopsla23', 'compression']
            if (index + repeat) % 2:
                modes.reverse()
            for mode in modes:
                log = output/f'{case.stem}-{mode}-{repeat}.log'
                command = [sys.executable, str(Path(__file__).resolve()),
                           '--worker', str(case), mode]
                start = time.monotonic()
                with log.open('w') as stream:
                    proc = subprocess.Popen(command, cwd=ROOT, stdout=stream,
                                            stderr=subprocess.STDOUT, start_new_session=True)
                    timed_out = False
                    try:
                        proc.wait(timeout=args.timeout)
                    except subprocess.TimeoutExpired:
                        timed_out = True
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait()
                text = log.read_text(errors='replace')
                payload = None
                for line in text.splitlines():
                    try:
                        value = json.loads(line)
                        if isinstance(value, dict) and 'status' in value:
                            payload = value
                    except ValueError:
                        pass
                memory = re.search(r'(\d+)\s+maximum resident set size', text)
                row = dict(case=case.name, mode=mode, repeat=repeat,
                           status='TIMEOUT' if timed_out else (payload or {}).get('status', 'ERROR'),
                           wall_seconds=time.monotonic()-start, exit_code=proc.returncode,
                           peak_mib=(payload or {}).get('peak_mib'),
                           result=payload, source_sha256=hashlib.sha256(case.read_bytes()).hexdigest())
                rows.append(row)
                with (output/'results.jsonl').open('a') as stream:
                    stream.write(json.dumps(row)+'\n')
                print(len(rows), '/',len(cases)*2*args.repeats, case.name, mode,
                      row['status'], round(row['wall_seconds'], 3), flush=True)
                (output/'progress.json').write_text(json.dumps(dict(completed=len(rows),
                    planned=len(cases)*2*args.repeats)))
    summary = {mode:dict(outcomes=dict(collections.Counter(r['status'] for r in rows if r['mode']==mode)),
                        total_wall_seconds=sum(r['wall_seconds'] for r in rows if r['mode']==mode))
               for mode in ('oopsla23', 'compression')}
    common = []
    for repeat in range(args.repeats):
        for case in cases:
            pair={r['mode']:r for r in rows if r['case']==case.name and r['repeat']==repeat}
            if all(r['status']=='CERTIFIED' for r in pair.values()):
                common.append(pair)
    summary['common_certified'] = len(common)
    if common:
        summary['compression_faster'] = sum(p['compression']['wall_seconds']<p['oopsla23']['wall_seconds'] for p in common)
        summary['median_paired_speedup_compression'] = statistics.median(
            p['oopsla23']['wall_seconds']/p['compression']['wall_seconds'] for p in common)
    (output/'summary.json').write_text(json.dumps(summary, indent=2))
    (output/'completed.json').write_text(json.dumps(dict(completed=len(rows))))
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
