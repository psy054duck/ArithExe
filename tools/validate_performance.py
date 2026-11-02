#!/usr/bin/env python3
"""Build, regression-test, and compare an isolated performance checkout."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--wait-for-unit')
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    state = dict(started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                 optimized=str(ROOT), baseline=str(baseline))
    env = os.environ.copy()
    solver_python = ROOT.parent / 'solver-venv/bin/python'
    bench_python = ROOT.parent / 'benchexec-venv/bin/python'
    env.update(ARITHEXE_SOLVER_PYTHON=str(solver_python),
               ARITHEXE_SOLVER_WORKER=str(ROOT / 'solver_worker.py'),
               ARITHEXE_SOLVER_TIMEOUT_MS='300000',
               ARITHEXE_MULTIVARIATE_STRATEGY='tupling',
               OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1', MKL_NUM_THREADS='1',
               PYTHONHASHSEED='0')

    def save(status, **details):
        state.update(state=status, **details)
        temporary = output / 'status.tmp'
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(output / 'status.json')
        print(status, flush=True)

    def command(label, argv, *, cwd=ROOT, timeout=1800):
        print(f'{label}: {argv}', flush=True)
        start = time.monotonic()
        with (output / (label + '.log')).open('w') as stream:
            run = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, timeout=timeout,
                                 stdout=stream, stderr=subprocess.STDOUT)
        state['commands'].append(dict(label=label, command=list(map(str, argv)),
                                      returncode=run.returncode,
                                      elapsed_seconds=time.monotonic() - start))
        if run.returncode:
            raise RuntimeError(f'{label} failed with exit code {run.returncode}')

    try:
        if args.wait_for_unit:
            save('WAITING_FOR_BASELINE', wait_for_unit=args.wait_for_unit)
            while True:
                unit = subprocess.run(['systemctl', 'show', args.wait_for_unit,
                                       '--property=ActiveState', '--value'],
                                      capture_output=True, text=True)
                if unit.stdout.strip() not in {'active', 'activating', 'deactivating'}:
                    break
                time.sleep(20)
        pivot = Path('rec_solver/rec_solver/core/multivariate.py')
        assert (ROOT / pivot).read_bytes() == (baseline / pivot).read_bytes(), 'Pivot implementation differs'
        save('BUILDING')
        command('configure', ['cmake', '-S', '.', '-B', 'build', '-DCMAKE_BUILD_TYPE=Debug',
                             '-DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm',
                             f'-DZ3_DIR={ROOT.parent}/dependencies/cmake-z3',
                             f'-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST={ROOT.parent}/dependencies/googletest',
                             f'-DFETCHCONTENT_SOURCE_DIR_SPDLOG={ROOT.parent}/dependencies/spdlog'])
        command('build', ['cmake', '--build', 'build', '-j2', '--target', 'arith_exe',
                         'test_logic', 'test_engine', 'test_algebra', 'test_path_expression'])
        save('TESTING')
        command('python-tests', [solver_python, '-m', 'unittest', 'discover', '-s', 'test/tupling', '-v'], timeout=600)
        command('benchexec-tests', [bench_python, '-m', 'unittest', 'discover', '-s', 'test/benchexec', '-v'])
        for target in ['test_logic', 'test_algebra', 'test_path_expression']:
            command(target, [ROOT / 'build' / target], timeout=600)
        command('engine-tests', [ROOT / 'build/test_engine',
                                '--gtest_filter=PathExpressionIntegration.*:ArraySummaryComposition.*'], timeout=600)
        save('COMPARING')
        tasks = ['brs1', 'brs1f', 'eqn1f', 'indp2', 'condm', 'mods']
        for label, root in [('baseline', baseline), ('optimized', ROOT)]:
            argv = [bench_python, root / 'tools/run_benchexec.py', '--scope', 'system',
                    '--output-dir', output / label, '--solver-python', solver_python,
                    '--timelimit', '300', '--walltimelimit', '360', '--workers', '2']
            for task in tasks:
                argv += ['--task', task]
            command(label, argv, cwd=root, timeout=2400)
        old = json.loads((output / 'baseline/summary.json').read_text())
        new = json.loads((output / 'optimized/summary.json').read_text())
        pairs = []
        old_rows = {row['task']: row for row in old['results']}
        for row in new['results']:
            before = old_rows[row['task']]
            both_correct = before['category'] == row['category'] == 'correct'
            pairs.append(dict(task=row['task'], baseline=before, optimized=row,
                              cpu_speedup=(float(before['cputime'].removesuffix('s')) /
                                           float(row['cputime'].removesuffix('s'))) if both_correct else None,
                              lost_expected_verdict=before['category'] == 'correct' and row['category'] != 'correct'))
        (output / 'comparison.json').write_text(json.dumps(pairs, indent=2) + '\n')
        xmls = list((output / 'baseline').glob('*.results.*.xml')) + list((output / 'optimized').glob('*.results.*.xml'))
        command('comparison-tables', [bench_python.parent / 'table-generator', '--name', 'performance',
                                       '--outputpath', output / 'tables', *xmls])
        with (ROOT / 'build/arith_exe').open('rb') as stream:
            binary_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
        save('COMPLETE', finished_utc=datetime.now(timezone.utc).isoformat(),
             no_verdict_regressions=not any(p['lost_expected_verdict'] for p in pairs),
             optimized_binary_sha256=binary_hash, comparisons=pairs,
             note='Six-task comparison only; no whole-suite speedup claim.')
        return 1 if any(p['lost_expected_verdict'] for p in pairs) else 0
    except Exception as error:
        save('FAILED', error=str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
