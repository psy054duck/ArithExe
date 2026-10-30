"""Build an isolated paper-mode snapshot and benchmark the previous selection."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback

ROOT = Path('/home/ubuntu/path-expression-experiment')
RUN = ROOT / 'oopsla23-server-20261006'
PREVIOUS = ROOT / 'arithexe-ite-fix-no-scaling-20261006'
ARCHIVE = ROOT / 'archives/oopsla23-server-20261006.tar.gz'

def save(name, value):
    (RUN/name).write_text(json.dumps(value, indent=2)+'\n')

def checked(command, name, **kwargs):
    with (RUN/name).open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, **kwargs)

def main():
    RUN.mkdir(exist_ok=True)
    assert not (RUN/'state.json').exists(), 'Refusing to overwrite an existing run'
    try:
        save('state.json', {'phase':'building'})
        source = RUN/'ArithExe'
        build = source/'build'
        assert source.is_dir(), 'Extract the source snapshot before launching'
        old = json.loads((PREVIOUS/'configuration.json').read_text())
        checked(['cmake','-S',str(source),'-B',str(build),'-G','Ninja',
                 '-DCMAKE_BUILD_TYPE=Debug','-DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm',
                 '-DZ3_DIR='+str(ROOT/'cmake-z3'),
                 '-DFETCHCONTENT_SOURCE_DIR_SPDLOG='+str(ROOT/'cmake-sources/spdlog-src'),
                 '-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST='+str(ROOT/'cmake-sources/googletest-src')], 'configure.log')
        checked(['cmake','--build',str(build),'--target','arith_exe','-j','2'], 'build.log')
        env = dict(os.environ, ARITHEXE_SOLVER_PYTHON=sys.executable,
                   ARITHEXE_SOLVER_WORKER=str(build/'solver_worker.py'), ARITHEXE_CLANG='/usr/bin/clang-20')
        checked([sys.executable,'-m','unittest','discover','-s','test','-p','test_paper_periodic.py'],
                'periodic-tests.log', cwd=source, env=env)
        tool = old['arithexe']
        tool['executable'] = str(build/'arith_exe')
        tool['executable_sha256'] = hashlib.sha256((build/'arith_exe').read_bytes()).hexdigest()
        tool['environment'].update(ARITHEXE_SOLVER_WORKER=str(build/'solver_worker.py'),
             ARITHEXE_COARSE_LEMMA_FILE=str(build/'lemma.smt2'), ARITHEXE_PERIODIC_STRATEGY='oopsla23',
             ARITHEXE_COARSE_FALLBACK='0', ARITHEXE_FORCE_PATH_EXPRESSIONS='0')
        tool['support_files_sha256'] = {str(p):hashlib.sha256(p.read_bytes()).hexdigest()
             for p in [build/'solver.py',build/'solver_worker.py',build/'lemma.smt2', *list((build/'rec_solver').rglob('*.py'))]}
        tool['source_archive_sha256'] = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
        config = {'dataset':old['dataset'],'arithexe':tool}
        save('configuration.json',config)
        shutil.copyfile(source/'experiments/linux/benchmark.py',RUN/'benchmark.py')
        save('state.json',{'phase':'running','timeout_seconds':30,'memory_mib':4096,
                          'baseline':'reconstructed OOPSLA23 periodic sampling; not original artifact'})
        checked([sys.executable,str(RUN/'benchmark.py'),'--config',str(RUN/'configuration.json'),
                 '--output',str(RUN/'full'),'--tools','arithexe','--timeout','30','--memory-mib','4096'],'full.log')
        completion = json.loads((RUN/'full/completed.json').read_text())
        save('state.json',{'phase':'completed','completion':completion})
    except Exception as error:
        save('state.json',{'phase':'failed','error':str(error)})
        traceback.print_exc()
        raise

if __name__ == '__main__':
    main()
