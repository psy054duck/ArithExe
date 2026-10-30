"""Isolated latest-code server build and 180s default-scheduling sumt5 test."""
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tarfile
import time
import traceback

ROOT = Path('/home/ubuntu/path-expression-experiment')
RUN = ROOT / 'sumt5-default-180s-20261006'
ARCHIVE = ROOT / 'archives/arithexe-sumt5-latest-20261006.tar.gz'

def save(value):
    (RUN / 'state.json').write_text(json.dumps(value, indent=2) + '\n')

def checked(command, name):
    with (RUN / name).open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)

def main():
    RUN.mkdir()
    try:
        save({'phase': 'building'})
        source = RUN / 'ArithExe'
        source.mkdir()
        with tarfile.open(ARCHIVE) as package:
            package.extractall(source, filter='data')
        build = source / 'build'
        checked(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Debug', '-DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm',
                 '-DZ3_DIR=' + str(ROOT / 'cmake-z3'),
                 '-DFETCHCONTENT_SOURCE_DIR_SPDLOG=' + str(ROOT / 'cmake-sources/spdlog-src'),
                 '-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=' + str(ROOT / 'cmake-sources/googletest-src')], 'configure.log')
        checked(['cmake', '--build', str(build), '--target', 'arith_exe', '-j', '2'], 'build.log')
        task = ROOT / 'arithexe-latest-no-nla-scaling-20261006/dataset/tasks/c/loops-crafted-1/sumt5.c'
        env = {k:v for k,v in os.environ.items() if not k.startswith('ARITHEXE_')}
        env.update(ARITHEXE_SOLVER_PYTHON=str(ROOT / 'venv/bin/python'),
                   ARITHEXE_SOLVER_WORKER=str(build / 'solver_worker.py'),
                   ARITHEXE_COARSE_LEMMA_FILE=str(build / 'lemma.smt2'), ARITHEXE_CLANG='/usr/bin/clang-20')
        command = [str(build / 'arith_exe'), '--verbose', '--ignore-bitwidth-constraints',
                   '--no-witness', '--data-model=ILP32', str(task)]
        metadata = dict(timeout_seconds=180, scheduling='defaults, no algorithm environment overrides',
                        command=command, environment={k:v for k,v in env.items() if k.startswith('ARITHEXE_')},
                        binary_sha256=hashlib.sha256((build / 'arith_exe').read_bytes()).hexdigest(),
                        source_archive_sha256=hashlib.sha256(ARCHIVE.read_bytes()).hexdigest(),
                        input_sha256=hashlib.sha256(task.read_bytes()).hexdigest())
        (RUN / 'configuration.json').write_text(json.dumps(metadata, indent=2)+'\n')
        save({'phase': 'running', 'timeout_seconds': 180})
        start = time.monotonic()
        with (RUN / 'verbose.log').open('w') as log:
            process = subprocess.Popen(command, env=env, cwd=source, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            timed_out = False
            try:
                process.wait(timeout=180)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        elapsed = time.monotonic() - start
        text = (RUN / 'verbose.log').read_text()
        verdicts = re.findall(r'^(TRUE\(integer-relaxed\)|FALSE\(integer-relaxed\)|UNKNOWN)\s*$', text, re.M)
        result = dict(phase='completed', seconds=elapsed, timed_out=timed_out,
                      exit_code=process.returncode, verdict='TIMEOUT' if timed_out else verdicts[-1] if verdicts else 'ERROR',
                      coarse_summaries=text.count('[coarse-path] Summarized loop'))
        (RUN / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
        save(result)
    except Exception as error:
        save({'phase': 'failed', 'error': str(error)})
        traceback.print_exc()
        raise

if __name__ == '__main__':
    main()
