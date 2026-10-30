"""Paired verifier ablation on the exact 217-task experiment selection."""
import collections
import csv
import datetime
import importlib.util
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
DATASET = ROOT.parent/'svcomp2026-single-assertions'


def main():
    runner_path = ROOT.parent/'svcomp2026-final-assertions/benchmarking/benchmark.py'
    spec = importlib.util.spec_from_file_location('bench_runner', runner_path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    previous = ROOT/'experiments/diagnostics/comparison-ite-fix-20261006/arithexe/results.jsonl'
    rows = [json.loads(line) for line in previous.read_text().splitlines()]
    rows = sorted((r for r in rows if r['task'] != 'c/loop-invariants/bin-suffix-5.yml'), key=lambda r:r['task'])
    assert len(rows) == 217
    transforms = {r['task']:r for r in json.loads((DATASET/'transformations.json').read_text()) if r['status']=='included'}
    hashes = {r['task']:r['processed_sha256'] for r in csv.DictReader((DATASET/'manifest.csv').open())}
    for row in rows:
        source = DATASET/'tasks'/transforms[row['task']]['input_files'][0]
        if runner.sha(source) != hashes[row['task']]:
            raise RuntimeError('Dataset hash differs from experiment: '+row['task'])
    sampler = runner.ResourceSampler(.1)
    sampler.start()
    output = ROOT/'experiments/diagnostics'/datetime.datetime.now().strftime('oopsla23-verifier-%Y%m%d-%H%M%S')
    output.mkdir()
    base_env = {k:v for k,v in os.environ.items() if not k.startswith('ARITHEXE_')}
    base_env.update(ARITHEXE_SOLVER_PYTHON='/opt/miniconda3/envs/veri/bin/python',
        ARITHEXE_SOLVER_WORKER=str(ROOT/'build/solver_worker.py'),
        ARITHEXE_CLANG='/opt/homebrew/opt/llvm/bin/clang',
        ARITHEXE_PATH_MAX_ROOT='6', ARITHEXE_PATH_BEAM='4', ARITHEXE_PATH_EVIDENCE='2',
        ARITHEXE_PRECISE_TRIAL_ITERATIONS='12', ARITHEXE_COARSE_TIMEOUT_MS='3000',
        ARITHEXE_COARSE_REFINE_STEPS='512', ARITHEXE_COARSE_LEMMA_FILE=str(ROOT/'build/lemma.smt2'))
    runner.atomic_json(output/'configuration.json', dict(tasks=[r['task'] for r in rows],
        timeout_seconds=30, jobs=1, memory_limit_mib=4096,
        platform=runner.platform.platform(), environment={k:v for k,v in base_env.items() if k.startswith('ARITHEXE_')},
        binary_sha256=runner.sha(ROOT/'build/arith_exe'),
        solver_hashes={str(p.relative_to(ROOT)):runner.sha(p) for p in (ROOT/'build/rec_solver/rec_solver/core').glob('*.py')},
        excluded_components=['loop-industry-pattern','loop-invgen','nla-digbench-scaling'],
        scope='local paired ablation; not directly comparable to server timing/cgroup memory',
        verdict_scope='reported outcomes, no transformed ground truth or witness validation'))
    print(output, flush=True)
    results = []
    try:
        for index, row in enumerate(rows):
            modes = ['oopsla23', 'proposed'] if index % 2 == 0 else ['proposed', 'oopsla23']
            for mode in modes:
                source = DATASET/'tasks'/transforms[row['task']]['input_files'][0]
                env = dict(base_env)
                env.update(ARITHEXE_PERIODIC_STRATEGY='oopsla23' if mode=='oopsla23' else 'compression',
                           ARITHEXE_COARSE_FALLBACK='0' if mode=='oopsla23' else '1',
                           ARITHEXE_FORCE_PATH_EXPRESSIONS='0' if mode=='oopsla23' else '1')
                directory = output/'runs'/mode/row['task'].removeprefix('c/').removesuffix('.yml')
                command = [str(ROOT/'build/arith_exe'),'--verbose','--ignore-bitwidth-constraints',
                           '--no-witness','--data-model='+row['data_model'],str(source)]
                result = runner.run_tool(command, env, directory, sampler, 30, 4096*2**20, 4*2**20)
                result.update(task=row['task'], mode=mode, source_sha256=runner.sha(source))
                text = Path(result['log']).read_text(errors='replace')
                if mode == 'oopsla23' and ('[path-expr]' in text or '[nested-path] Accelerating' in text):
                    raise RuntimeError('Online acceleration leaked into baseline: '+row['task'])
                results.append(result)
                with (output/'results.jsonl').open('a') as stream:
                    stream.write(json.dumps(result)+'\n')
                runner.atomic_json(output/'progress.json', dict(completed=len(results),planned=434,
                    outcomes={m:dict(collections.Counter(r['status'] for r in results if r['mode']==m)) for m in modes},
                    sampler_errors=list(sampler.errors)))
                print(len(results), '/434', mode, row['task'], result['status'], result['wall_seconds'], flush=True)
        runner.atomic_json(output/'completed.json', dict(completed=len(results),planned=434))
    finally:
        sampler.close()


if __name__ == '__main__':
    main()
