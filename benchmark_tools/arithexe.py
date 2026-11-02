"""BenchExec integration for ArrayRec, built on ArithExe."""

import hashlib
import os
from pathlib import Path

from benchexec import result
from benchexec.tools.sv_benchmarks_util import ILP32, LP64, get_data_model_from_task
from benchexec.tools.template import BaseTool2, UnsupportedFeatureException


class Tool(BaseTool2):
    def executable(self, tool_locator):
        return tool_locator.find_executable("arith_exe", subdir="build")

    def name(self):
        return "ArrayRec"

    def project_url(self):
        return "https://github.com/psy054duck/ArithExe"

    def version(self, executable):
        with open(executable, "rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        return "sha256-" + digest[:16]

    def working_directory(self, executable):
        return str(Path(executable).resolve().parent.parent)

    def environment(self, executable):
        root = Path(self.working_directory(executable))
        strategy = os.environ.get("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling")
        if strategy not in {"tupling", "pivot"}:
            raise UnsupportedFeatureException("Unknown multivariate strategy")
        return {
            "keepEnv": {name: "" for name in ("PATH", "LD_LIBRARY_PATH", "LANG")},
            "newEnv": {
                "ARITHEXE_MULTIVARIATE_STRATEGY": strategy,
                "ARITHEXE_SOLVER_PYTHON": os.environ.get(
                    "ARITHEXE_SOLVER_PYTHON",
                    str(root.parent / "solver-venv/bin/python")),
                "ARITHEXE_SOLVER_WORKER": str(root / "solver_worker.py"),
                "ARITHEXE_SOLVER_TIMEOUT_MS": os.environ.get(
                    "ARITHEXE_SOLVER_TIMEOUT_MS", "300000"),
                "ARITHEXE_FORCE_PATH_EXPRESSIONS": "0",
                "OPENBLAS_NUM_THREADS": "1",
                "OMP_NUM_THREADS": "1",
                "MKL_NUM_THREADS": "1",
                "PYTHONHASHSEED": "0",
            },
        }

    def cmdline(self, executable, options, task, rlimits):
        if not task.property_file:
            raise UnsupportedFeatureException("A reachability property is required")
        model = get_data_model_from_task(task, {ILP32: ILP32, LP64: LP64})
        if model is None:
            raise UnsupportedFeatureException("An explicit C data model is required")
        return [executable, *options, "--no-witness", f"--data-model={model}",
                "--property-file", task.property_file, task.single_input_file]

    def determine_result(self, run):
        if run.was_timeout:
            return result.RESULT_TIMEOUT
        if run.termination_reason == "memory":
            return "OUT OF MEMORY"
        if run.exit_code.signal or run.exit_code.value != 0:
            return result.RESULT_ERROR
        if any("ModuleNotFoundError:" in line or "Fatal Python error:" in line
               for line in run.output):
            return "ERROR (solver setup)"
        verdicts = {
            "TRUE": result.RESULT_TRUE_PROP,
            "FALSE(unreach-call)": result.RESULT_FALSE_REACH,
            "FALSE": result.RESULT_FALSE_REACH,
            "UNKNOWN": result.RESULT_UNKNOWN,
        }
        relaxed = any(option in run.cmdline for option in (
            "--ignore-bitwidth-constraints", "--ignore-32bit-constraints"))
        relaxed_verdicts = {
            "TRUE(integer-relaxed)": result.RESULT_TRUE_PROP,
            "FALSE(integer-relaxed)": result.RESULT_FALSE_REACH,
        }
        for line in reversed(run.output):
            token = line.strip()
            if token in relaxed_verdicts:
                return relaxed_verdicts[token] if relaxed else "ERROR (semantics mismatch)"
            if token in verdicts:
                if relaxed and token != "UNKNOWN":
                    return "ERROR (semantics mismatch)"
                return verdicts[token]
        return result.RESULT_ERROR
