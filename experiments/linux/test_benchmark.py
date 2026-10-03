import unittest
from benchmark import classify, classify_icra
from benchexec.util import ProcessExitCode


class ClassificationTest(unittest.TestCase):
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
