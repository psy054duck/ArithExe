#!/usr/bin/env bash
set -euo pipefail
experiment_dir="${1:?Pass the absolute experiment root}"
cd "$experiment_dir"
venv/bin/pip install 'networkx==3.4.2' numpy
# Pip's matching Z3 package includes the C++ headers and Linux shared solver.
# Debian's libz3-dev does not ship the CMake config required by this project.
venv/bin/python - "$experiment_dir" <<'PY'
import pathlib, sys, z3
root = pathlib.Path(sys.argv[1])
solver = pathlib.Path(z3.__file__).parent
config = root / 'cmake-z3'
config.mkdir(exist_ok=True)
(config / 'Z3Config.cmake').write_text(
    f'set(Z3_CXX_INCLUDE_DIRS "{solver / "include"}")\n'
    f'set(Z3_LIBRARIES "{solver / "lib/libz3.so"}")\n'
    'set(Z3_VERSION_STRING "4.15.3")\n')
PY
cmake -S ArithExe -B ArithExe/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm -DZ3_DIR="$experiment_dir/cmake-z3" \
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG="$experiment_dir/cmake-sources/spdlog-src" \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$experiment_dir/cmake-sources/googletest-src"
cmake --build ArithExe/build -j1
export ARITHEXE_SOLVER_PYTHON="$experiment_dir/venv/bin/python"
export ARITHEXE_SOLVER_WORKER="$experiment_dir/ArithExe/build/solver_worker.py"
export ARITHEXE_CLANG=/usr/bin/clang-18
ctest --test-dir ArithExe/build/test \
  -R 'PathExpression|RelaxedInteger|Phi|Nested|Nondet|Witness' \
  --output-on-failure --timeout 90
