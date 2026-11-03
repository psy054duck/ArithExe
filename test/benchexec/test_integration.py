import os
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from xml.etree import ElementTree as ET

import yaml

from benchexec import result
from benchexec.tools.template import BaseTool2, UnsupportedFeatureException
from benchexec.util import ProcessExitCode

from benchmark_tools.arithexe import Tool
from tools.run_benchexec import (
    make_definition, read_results, launch_command, interpreter_path, integer_tasks,
)
from tools.submit_benchexec import submission_command


class AdapterTests(unittest.TestCase):
    def test_fold_registry_reaches_the_solver_environment(self):
        with patch.dict(os.environ, {"ARITHEXE_FOLD_OPERATORS": "/snapshot/operators.json"}):
            environment = Tool().environment("/workspace/ArithExe/build/arith_exe")
        self.assertEqual(environment["newEnv"]["ARITHEXE_FOLD_OPERATORS"],
                         "/snapshot/operators.json")

    def test_tool_name(self):
        self.assertEqual(Tool().name(), "ArrayRec")

    def task(self, model="ILP32", property_file="unreach-call.prp"):
        return BaseTool2.Task.with_files(
            ["program.c"], property_file=property_file,
            options={"language": "C", "data_model": model})

    def run_output(self, output, *, code=0, signal=None, reason=None, command=()):
        return BaseTool2.Run(list(command), ProcessExitCode.create(value=code, signal=signal),
                             BaseTool2.RunOutput(output.splitlines(keepends=True)), reason)

    def test_ilp32_command_uses_property_and_disables_shared_witness(self):
        command = Tool().cmdline("build/arith_exe", [], self.task(), None)
        self.assertEqual(command, ["build/arith_exe", "--no-witness", "--data-model=ILP32",
                                   "--property-file", "unreach-call.prp", "program.c"])

    def test_lp64_command(self):
        command = Tool().cmdline("exe", [], self.task("LP64"), None)
        self.assertIn("--data-model=LP64", command)

    def test_unknown_model_rejected(self):
        with self.assertRaises(UnsupportedFeatureException):
            Tool().cmdline("exe", [], self.task("UNKNOWN"), None)

    def test_missing_model_rejected(self):
        task = BaseTool2.Task.with_files(["p.c"], property_file="unreach-call.prp")
        with self.assertRaises(UnsupportedFeatureException):
            Tool().cmdline("exe", [], task, None)

    def test_missing_property_rejected(self):
        with self.assertRaises(UnsupportedFeatureException):
            Tool().cmdline("exe", [], self.task(property_file=None), None)

    def test_true_false_unknown_and_missing_output(self):
        for text, expected in [("TRUE", result.RESULT_TRUE_PROP),
                               ("FALSE(unreach-call)", result.RESULT_FALSE_REACH),
                               ("FALSE", result.RESULT_FALSE_REACH),
                               ("UNKNOWN", result.RESULT_UNKNOWN),
                               ("[info] program is TRUE?", result.RESULT_ERROR)]:
            with self.subTest(text=text):
                self.assertEqual(Tool().determine_result(self.run_output(text)), expected)

    def test_final_verdict_wins(self):
        run = self.run_output("TRUE\nUNKNOWN\n")
        self.assertEqual(Tool().determine_result(run), result.RESULT_UNKNOWN)

    def test_integer_verdicts_require_explicit_integer_mode(self):
        for text, expected in (("TRUE(integer-relaxed)", result.RESULT_TRUE_PROP),
                               ("FALSE(integer-relaxed)", result.RESULT_FALSE_REACH)):
            with self.subTest(text=text):
                run = self.run_output(text, command=["exe", "--ignore-bitwidth-constraints"])
                self.assertEqual(Tool().determine_result(run), expected)
                self.assertEqual(Tool().determine_result(self.run_output(text)),
                                 "ERROR (semantics mismatch)")

    def test_c_verdicts_cannot_silently_enter_integer_run(self):
        run = self.run_output("TRUE", command=["exe", "--ignore-bitwidth-constraints"])
        self.assertEqual(Tool().determine_result(run), "ERROR (semantics mismatch)")

    def test_integer_crash_does_not_count_as_a_proof(self):
        run = self.run_output("TRUE(integer-relaxed)", code=1,
                              command=["exe", "--ignore-bitwidth-constraints"])
        self.assertEqual(Tool().determine_result(run), result.RESULT_ERROR)

    def test_crash_does_not_count_as_a_proof(self):
        self.assertEqual(Tool().determine_result(self.run_output("TRUE", code=1)),
                         result.RESULT_ERROR)

    def test_signal_does_not_count_as_a_proof(self):
        run = self.run_output("TRUE", code=None, signal=6)
        self.assertEqual(Tool().determine_result(run), result.RESULT_ERROR)

    def test_missing_solver_dependency_does_not_count_as_a_proof(self):
        run = self.run_output("ModuleNotFoundError: No module named 'z3'\nTRUE\n")
        self.assertEqual(Tool().determine_result(run), "ERROR (solver setup)")

    def test_timeout_and_memory_take_precedence(self):
        for reason, expected in [("cputime", result.RESULT_TIMEOUT),
                                 ("walltime", result.RESULT_TIMEOUT),
                                 ("memory", "OUT OF MEMORY")]:
            with self.subTest(reason=reason):
                run = self.run_output("TRUE", reason=reason)
                self.assertEqual(Tool().determine_result(run), expected)

    def test_environment_is_clean_and_explicit(self):
        with patch.dict(os.environ, {"LD_PRELOAD": "bad.so",
                                      "ARITHEXE_FORCE_PATH_EXPRESSIONS": "1",
                                      "ARITHEXE_MULTIVARIATE_STRATEGY": "tupling",
                                      "ARITHEXE_SOLVER_TIMEOUT_MS": "300000"}):
            env = Tool().environment("/workspace/ArithExe/build/arith_exe")
        self.assertNotIn("LD_PRELOAD", env["keepEnv"])
        self.assertEqual(env["newEnv"]["ARITHEXE_FORCE_PATH_EXPRESSIONS"], "0")
        self.assertEqual(env["newEnv"]["ARITHEXE_SOLVER_TIMEOUT_MS"], "300000")
        self.assertEqual(env["newEnv"]["OMP_NUM_THREADS"], "1")


class RunnerTests(unittest.TestCase):
    def test_system_scope_preserves_fold_registry(self):
        env = {key: "test" for key in ("PATH", "PYTHONPATH", "ARITHEXE_MULTIVARIATE_STRATEGY",
               "ARITHEXE_SOLVER_PYTHON", "ARITHEXE_SOLVER_TIMEOUT_MS")}
        env["ARITHEXE_FOLD_OPERATORS"] = "/snapshot/operators.json"
        command = launch_command(["benchexec"], SimpleNamespace(scope="system"), env)
        self.assertIn("ARITHEXE_FOLD_OPERATORS=/snapshot/operators.json", command)

    def test_integer_definition_enables_and_labels_the_mode(self):
        args = SimpleNamespace(timelimit=300, walltimelimit=360, memory="3 GB",
                               workers=2, strategy="tupling", semantics="integer")
        tasks = [dict(metadata="integer/a.yml", property_file="p.prp")]
        root = make_definition(tasks, args).getroot()
        self.assertEqual(root.get("displayName"), "ArrayRec (integer semantics)")
        run = root.find("rundefinition")
        self.assertEqual(run.get("name"), "tupling-integer")
        self.assertEqual(run.find("option").get("name"), "--ignore-bitwidth-constraints")

    def test_integer_tasks_keep_inputs_but_remove_c_ground_truth(self):
        with TemporaryDirectory() as folder:
            source = Path(folder) / "a.c"
            source.touch()
            metadata = Path(folder) / "a.yml"
            document = {"format_version": "2.0", "input_files": "a.c",
                        "properties": [{"property_file": "p.prp", "expected_verdict": True}],
                        "options": {"language": "C", "data_model": "ILP32"}}
            metadata.write_text(yaml.safe_dump(document))
            task = dict(name="a", metadata=str(metadata), source=str(source),
                        property_file=str(Path(folder) / "p.prp"), expected=True)
            generated = integer_tasks([task], Path(folder) / "integer-tasks")[0]
            integer_document = yaml.safe_load(Path(generated["metadata"]).read_text())
            self.assertEqual(integer_document["input_files"], str(source))
            self.assertNotIn("expected_verdict", integer_document["properties"][0])
            self.assertIsNone(generated["expected"])
            self.assertTrue(task["expected"])
            self.assertEqual(yaml.safe_load(metadata.read_text()), document)

    def test_interpreter_path_preserves_virtual_environment_symlink(self):
        with TemporaryDirectory() as folder:
            real = Path(folder) / "system-python"
            real.touch()
            venv = Path(folder) / "venv-python"
            venv.symlink_to(real)
            self.assertEqual(interpreter_path(venv), venv)
            self.assertNotEqual(interpreter_path(venv), real)

    def test_xml_uses_yaml_tasks_and_explicit_limits(self):
        args = SimpleNamespace(timelimit=300, walltimelimit=360, memory="3 GB",
                               workers=2, strategy="tupling")
        tasks = [dict(metadata="/svcomp/a.yml", property_file="/svcomp/p.prp"),
                 dict(metadata="/svcomp/b.yml", property_file="/svcomp/p.prp")]
        root = make_definition(tasks, args).getroot()
        self.assertEqual(root.get("displayName"), "ArrayRec")
        self.assertEqual(root.get("cpuCores"), "1")
        self.assertEqual(root.get("walltimelimit"), "360s")
        group = root.find("rundefinition/tasks")
        self.assertEqual([e.text for e in group.findall("include")],
                         ["/svcomp/a.yml", "/svcomp/b.yml"])
        self.assertEqual(group.findtext("propertyfile"), "/svcomp/p.prp")

    def test_mixed_properties_rejected(self):
        args = SimpleNamespace(timelimit=300, walltimelimit=360, memory="3 GB",
                               workers=2, strategy="tupling")
        tasks = [dict(metadata="a.yml", property_file="p.prp"),
                 dict(metadata="b.yml", property_file="other.prp")]
        with self.assertRaises(ValueError):
            make_definition(tasks, args)

    def test_native_categories_are_preserved(self):
        with TemporaryDirectory() as folder:
            root = ET.Element("result")
            for name, status, category in [("a", "true", "correct"),
                                            ("b", "false(unreach-call)", "wrong"),
                                            ("c", "TIMEOUT", "error")]:
                run = ET.SubElement(root, "run", name=f"{name}.yml")
                ET.SubElement(run, "column", title="status", value=status)
                ET.SubElement(run, "column", title="category", value=category)
            ET.SubElement(root, "run", name="not-started.yml")
            ET.ElementTree(root).write(Path(folder) / "array.results.tupling.xml")
            rows = read_results(Path(folder))
        self.assertEqual([r["category"] for r in rows], ["correct", "wrong", "error"])

    def test_persistent_job_keeps_logging_and_uses_a_separate_execution_scope(self):
        command = submission_command(Path("/results/run"), "test-job", ["--task", "eqn1f"])
        self.assertIn("--property=StandardOutput=append:/results/run.service.log", command)
        self.assertIn(f"--uid={os.getuid()}", command)
        self.assertNotIn("--wait", command)
        self.assertIn("--property=MemorySwapMax=0", command)
        index = command.index("--scope")
        self.assertEqual(command[index + 1], "system")
        self.assertIn("ARITHEXE_BENCHEXEC_PARENT_UNIT=test-job.service", command)
        self.assertEqual(command[-2:], ["--task", "eqn1f"])

    def test_system_scope_runs_service_as_current_user(self):
        env = {key: "value" for key in ("PATH", "PYTHONPATH", "ARITHEXE_MULTIVARIATE_STRATEGY",
                                        "ARITHEXE_SOLVER_PYTHON", "ARITHEXE_SOLVER_TIMEOUT_MS")}
        command = launch_command(["benchexec", "test.xml"],
                                 SimpleNamespace(scope="system"), env)
        self.assertEqual(command[:3], ["sudo", "-n", "systemd-run"])
        self.assertIn(f"--uid={os.getuid()}", command)
        self.assertIn("--property=Delegate=yes", command)

    def test_execution_scope_is_bound_to_persistent_wrapper(self):
        env = {key: "value" for key in ("PATH", "PYTHONPATH", "ARITHEXE_MULTIVARIATE_STRATEGY",
                                        "ARITHEXE_SOLVER_PYTHON", "ARITHEXE_SOLVER_TIMEOUT_MS")}
        env["ARITHEXE_BENCHEXEC_PARENT_UNIT"] = "wrapper.service"
        command = launch_command(["benchexec"], SimpleNamespace(scope="system"), env)
        self.assertIn("--property=BindsTo=wrapper.service", command)
        self.assertIn("--property=After=wrapper.service", command)
        self.assertIn("--property=MemorySwapMax=0", command)


if __name__ == "__main__":
    unittest.main()
