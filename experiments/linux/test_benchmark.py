import unittest
import tempfile
from pathlib import Path
from benchmark import prepare_delegated_service
from benchmark import classify, classify_icra, icra_command, select_records, resource_model, nominal_label_match, tool_kind, memory_fields, memory_summary
from benchexec.util import ProcessExitCode


class ClassificationTest(unittest.TestCase):
    def test_task_exclusions_apply_before_limit(self):
        rows = [{"task": "a", "component": "loops", "status": "included"},
                {"task": "b", "component": "loops", "status": "included"}]
        self.assertEqual([r["task"] for r in select_records(rows, limit=1, excluded_tasks=["a"])], ["b"])

    def test_delegated_runner_has_exclusive_sibling(self):
        with tempfile.TemporaryDirectory() as directory:
            service = Path(directory) / "path-expression-test.service"
            manager = service / "manager"
            manager.mkdir(parents=True)
            (service / "cgroup.controllers").write_text("cpu memory pids")
            (service / "cgroup.subtree_control").write_text("")
            (manager / "cgroup.procs").write_text("41\n42\n")
            runner = prepare_delegated_service(manager, 42)
            self.assertEqual(runner.parent, service)
            self.assertEqual((runner / "cgroup.procs").read_text(), "42")
            self.assertEqual((manager / "cgroup.procs").read_text(), "41\n42\n")
            self.assertEqual((service / "cgroup.subtree_control").read_text(), "+cpu +memory")

    def test_other_cgroups_are_not_modified(self):
        with tempfile.TemporaryDirectory() as directory:
            current = Path(directory) / "unrelated.service" / "manager"
            self.assertEqual(prepare_delegated_service(current, 42), current)
            self.assertFalse(current.exists())

    def test_component_exclusions_apply_before_limit(self):
        rows = [{"task": "a", "component": "loop-invgen", "status": "included"},
                {"task": "b", "component": "loops", "status": "included"},
                {"task": "c", "component": "loop-industry-pattern", "status": "included"}]
        self.assertEqual([r["task"] for r in select_records(rows, limit=1,
                         excluded_components=["loop-invgen", "loop-industry-pattern"])], ["b"])
        with self.assertRaises(ValueError):
            select_records(rows, components=["loop-invgen"], excluded_components=["loop-invgen"])

    def test_peak_memory_units(self):
        result = memory_fields({"memory": 1536 * 1024**2})
        self.assertEqual(result["peak_memory_bytes"], 1536 * 1024**2)
        self.assertEqual(result["memory"], result["peak_memory_bytes"])
        self.assertEqual(result["peak_memory_mib"], 1536)
        self.assertTrue(result["memory_measurement_available"])

    def test_missing_memory_is_not_zero(self):
        for value in (None, 0, -1, True, "123"):
            result = memory_fields({"memory": value})
            self.assertIsNone(result["peak_memory_bytes"])
            self.assertIsNone(result["peak_memory_mib"])
            self.assertFalse(result["memory_measurement_available"])

    def test_memory_summary_uses_nearest_rank_p95(self):
        rows = [{"memory": n * 1024**2} for n in range(1, 21)] + [{"memory": None}]
        result = memory_summary(rows)
        self.assertEqual(result["measured_runs"], 20)
        self.assertEqual(result["missing_runs"], 1)
        self.assertEqual(result["median"], 10.5)
        self.assertEqual(result["p95"], 19)
        self.assertEqual(result["max"], 20)
        self.assertIsNone(memory_summary([])["median"])

    def test_separate_icra_variants_share_backend_not_options(self):
        self.assertEqual(tool_kind("icra_split"), "icra")
        self.assertEqual(tool_kind("icra_refine"), "icra")
        self.assertEqual(tool_kind("automizer"), "automizer")
        for name, option, absent in [("icra_split", "-cra-split-loops", "-cra-refine"),
                                     ("icra_refine", "-cra-refine", "-cra-split-loops")]:
            command = icra_command({"executable": "/tool/icra", "loadpath": "/tool/lib", "options": [option]}, "LP64", "/input.c")
            self.assertIn(option, command)
            self.assertNotIn(absent, command)
            measurement = {"exitcode": ProcessExitCode.create(value=0)}
            passed = "Assertion Checking at Error Points\nChecking assertion at vertex 5\nIs not SAT! (Assertion on line 7 PASSED)\nBounds on Variables\n"
            self.assertEqual(classify(name, passed, measurement)[0], "TRUE")
            self.assertEqual(classify(name, passed, {"exitcode": ProcessExitCode.create(signal=9), "terminationreason": "memory"})[0], "MEMORY_LIMIT")

    def test_split_parent_verdict_is_not_a_per_assertion_label(self):
        record = {"original_expected_verdict": "False", "expected_verdict_valid_for_split": False}
        self.assertIsNone(nominal_label_match(record, "TRUE"))
        self.assertIsNone(nominal_label_match(record, "FALSE"))

    def test_legacy_nominal_labels_are_preserved(self):
        self.assertTrue(nominal_label_match({"original_expected_verdict": "True"}, "TRUE"))
        self.assertFalse(nominal_label_match({"original_expected_verdict": "True"}, "FALSE"))
        self.assertIsNone(nominal_label_match({"original_expected_verdict": "True"}, "UNKNOWN"))
        self.assertIsNone(nominal_label_match({"original_expected_verdict": None}, "FALSE"))

    def test_resource_description_tracks_actual_limits(self):
        self.assertIn("4096MiB includes descendants", resource_model(30, 4096))
        self.assertIn("30s wall limit", resource_model(30, 4096))
        self.assertIn("60s wall limit", resource_model(60, 6144))
        self.assertIn("6144MiB includes descendants", resource_model(60, 6144))
        self.assertIn("sequential", resource_model(30, 4096))

    def test_icra_default_options_preserve_baseline(self):
        tool = {"executable": "/tool/icra", "loadpath": "/tool/lib"}
        self.assertEqual(icra_command(tool, "ILP32", "/input.c"),
                         ["/tool/icra", "--newton-verbosity", "0", "-cra-split-loops",
                          "-loadpath", "/tool/lib", "-cflags", "-m32", "/input.c"])

    def test_icra_refinement_is_explicit_and_recorded(self):
        tool = {"executable": "/tool/icra", "loadpath": "/tool/lib",
                "options": ["-cra-split-loops", "-cra-refine"]}
        command = icra_command(tool, "LP64", "/input.c")
        self.assertEqual(command.count("-cra-refine"), 1)
        self.assertEqual(command.count("-cra-split-loops"), 1)
        self.assertIn("-m64", command)
        self.assertNotIn("-cra-refine-full", command)

    def test_icra_empty_options_and_invalid_options(self):
        tool = {"executable": "/tool/icra", "loadpath": "/tool/lib", "options": []}
        self.assertNotIn("-cra-split-loops", icra_command(tool, "ILP32", "/input.c"))
        tool["options"] = "-cra-refine"
        with self.assertRaises(ValueError):
            icra_command(tool, "ILP32", "/input.c")

    def test_component_selection(self):
        records = [{"task": "z", "component": "nla", "status": "included"},
                   {"task": "a", "component": "nla", "status": "included"},
                   {"task": "b", "component": "loops", "status": "included"},
                   {"task": "c", "component": "nla", "status": "excluded"}]
        self.assertEqual([r["task"] for r in select_records(records, ["nla"])], ["a", "z"])
        self.assertEqual([r["task"] for r in select_records(records, ["nla"], 1)], ["a"])
        self.assertEqual(len(select_records(records)), 3)
        with self.assertRaises(ValueError):
            select_records(records, ["missing"])

    def test_relaxed_true(self):
        measurement = {"exitcode": ProcessExitCode.create(value=0)}
        self.assertEqual(classify("arithexe", "TRUE(integer-relaxed)\n", measurement)[0], "TRUE")

    def test_relaxed_false(self):
        measurement = {"exitcode": ProcessExitCode.create(value=0)}
        self.assertEqual(classify("arithexe", "FALSE(integer-relaxed)\n", measurement)[0], "FALSE")

    def test_timeout_overrides_partial_proof(self):
        measurement = {"exitcode": ProcessExitCode.create(signal=9), "terminationreason": "walltime"}
        self.assertEqual(classify("arithexe", "TRUE\n", measurement)[0], "TIMEOUT")

    def test_memory_overrides_partial_proof(self):
        measurement = {"exitcode": ProcessExitCode.create(signal=9), "terminationreason": "memory"}
        self.assertEqual(classify("arithexe", "TRUE\n", measurement)[0], "MEMORY_LIMIT")

    def test_crash_overrides_partial_proof(self):
        measurement = {"exitcode": ProcessExitCode.create(signal=11)}
        self.assertEqual(classify("arithexe", "TRUE\n", measurement)[0], "CRASH")

    def test_icra_passed(self):
        text = "Assertion Checking at Error Points\nChecking assertion at vertex 5\nIs not SAT! (Assertion on line 7 PASSED)\nBounds on Variables\n"
        self.assertEqual(classify_icra(text), "TRUE")

    def test_icra_failed_is_not_false(self):
        text = "Assertion Checking at Error Points\nChecking assertion at vertex 5\nIs SAT! (Assertion on line 7 FAILED)\nBounds on Variables\n"
        self.assertEqual(classify_icra(text), "UNKNOWN")

    def test_icra_incomplete_is_not_true(self):
        self.assertEqual(classify_icra("Is not SAT! (Assertion on line 7 PASSED)\n"), "UNKNOWN")


if __name__ == "__main__":
    unittest.main()
