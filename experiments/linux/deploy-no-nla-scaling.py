"""Build the uploaded working-tree snapshot and run the audited reduced selection."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import traceback

ROOT = Path('/home/ubuntu/path-expression-experiment')
RUN = ROOT / 'arithexe-latest-no-nla-scaling-20261006'
ARCHIVE = ROOT / 'archives/arithexe-latest-no-nla-scaling-20261006.tar.gz'
PREVIOUS = ROOT / 'arithexe-query-fix-cgroup-fixed-20261006'

def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')

def checked(command, name, **kwargs):
    with (RUN / name).open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, **kwargs)

def main():
    global RUN, ARCHIVE, PREVIOUS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-name', default=RUN.name)
    parser.add_argument('--previous', default=PREVIOUS.name)
    parser.add_argument('--selection-policy', type=Path)
    parser.add_argument('--selection-script', type=Path)
    args = parser.parse_args()
    if any(Path(name).name != name for name in (args.run_name, args.previous)):
        raise ValueError('Run names must be single directory names')
    RUN = ROOT / args.run_name
    ARCHIVE = ROOT / 'archives' / (args.run_name + '.tar.gz')
    PREVIOUS = ROOT / args.previous
    RUN.mkdir()
    try:
        save(RUN / 'state.json', {'phase': 'building'})
        source = RUN / 'ArithExe'
        source.mkdir()
        with tarfile.open(ARCHIVE) as package:
            package.extractall(source, filter='data')
        old = json.loads((PREVIOUS / 'configuration.json').read_text())
        dataset = RUN / 'dataset'
        shutil.copytree(old['dataset'], dataset)
        policy = json.loads((args.selection_policy or (dataset / 'benchmark-selection.json')).read_text())
        if args.selection_script:
            shutil.copyfile(args.selection_script, dataset / 'select_benchmarks.py')
        policy['excluded_components'] = list(dict.fromkeys(policy['excluded_components'] + ['nla-digbench-scaling']))
        save(dataset / 'benchmark-selection.json', policy)
        checked([sys.executable, str(dataset / 'select_benchmarks.py'), '--root', str(dataset)], 'selection.log')
        checked([sys.executable, str(dataset / 'select_benchmarks.py'), '--root', str(dataset), '--check'], 'selection-audit.log')
        count = json.loads((dataset / 'benchmark-selection-summary.json').read_text())['tasks']
        build = source / 'build'
        checked(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Debug', '-DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm',
                 '-DZ3_DIR=' + str(ROOT / 'cmake-z3'),
                 '-DFETCHCONTENT_SOURCE_DIR_SPDLOG=' + str(ROOT / 'cmake-sources/spdlog-src'),
                 '-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=' + str(ROOT / 'cmake-sources/googletest-src')], 'configure.log')
        checked(['cmake', '--build', str(build), '--target', 'arith_exe', 'test_engine', 'test_logic',
                 'test_relaxed_integer', 'test_path_expression', '-j', '2'], 'build.log')
        env = dict(os.environ, ARITHEXE_SOLVER_PYTHON=sys.executable,
                   ARITHEXE_SOLVER_WORKER=str(build / 'solver_worker.py'),
                   ARITHEXE_COARSE_LEMMA_FILE=str(build / 'lemma.smt2'), ARITHEXE_CLANG='/usr/bin/clang-20')
        checked(['ctest', '--test-dir', str(build / 'test'), '-R',
                 '^(SafetyFeasibility|PreciseFirstScheduling|RationalQuery|QuantifiedQuery|OrdinaryCSemantics|PathExpressionIntegration|ITE2PIECEWISE|EQ_FINDING)',
                 '--timeout', '90', '--output-on-failure'], 'regressions.log', env=env)
        checked([sys.executable, '-m', 'unittest', 'discover', '-s', 'test', '-p', 'test_coarse_solver.py'],
                'worker-tests.log', cwd=source, env=env)
        checked([sys.executable, str(source / 'experiments/linux/prepare-arithexe-rerun.py'),
                 '--previous-config', str(PREVIOUS / 'configuration.json'), '--source', str(source),
                 '--source-archive', str(ARCHIVE), '--base-revision', 'working-tree-20261006',
                 '--output', str(RUN / 'configuration-base.json')], 'fingerprint.log')
        config = json.loads((RUN / 'configuration-base.json').read_text())
        config['dataset'] = str(dataset)
        config['arithexe']['support_files_sha256'].update({str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                                       for p in (build / 'rec_solver').rglob('*.py')})
        save(RUN / 'configuration.json', config)
        # Use the already validated cgroup-isolation runner, not an older snapshot.
        runner = ROOT / 'archives/benchmark.py'
        save(RUN / 'state.json', {'phase': 'running', 'planned_tasks': count,
                                 'excluded_components': policy['excluded_components'],
                                 'timeout_seconds': 30, 'memory_mib': 4096,
                                 'source_archive_sha256': hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()})
        checked([sys.executable, str(runner), '--config', str(RUN / 'configuration.json'),
                 '--output', str(RUN / 'full'), '--tools', 'arithexe', '--timeout', '30',
                 '--memory-mib', '4096'], 'full.log')
        completed = json.loads((RUN / 'full/completed.json').read_text())
        assert completed['runs'] == completed['planned'] == count
        save(RUN / 'state.json', {'phase': 'completed', 'completion': completed})
    except Exception as error:
        save(RUN / 'state.json', {'phase': 'failed', 'error': str(error)})
        traceback.print_exc()
        raise

if __name__ == '__main__':
    main()
