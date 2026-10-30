"""Regression checks for the isolated periodic-sampling verifier route."""
import os
from pathlib import Path
import signal
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
DATASET = ROOT.parent/'svcomp2026-single-assertions/tasks/c'


class PaperIntegration(unittest.TestCase):
    def run_case(self, relative, allow_timeout=False):
        env = dict(os.environ, ARITHEXE_PERIODIC_STRATEGY='oopsla23',
                   ARITHEXE_COARSE_FALLBACK='0',
                   ARITHEXE_SOLVER_PYTHON='/opt/miniconda3/envs/veri/bin/python',
                   ARITHEXE_SOLVER_WORKER=str(ROOT/'build/solver_worker.py'))
        proc = subprocess.Popen([str(ROOT/'build/arith_exe'), '--ignore-bitwidth-constraints',
                                 '--no-witness', str(DATASET/relative)], cwd=ROOT,
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                start_new_session=True)
        try:
            text, _ = proc.communicate(timeout=2 if allow_timeout else 30)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            text, _ = proc.communicate()
            if allow_timeout:
                return text.decode(errors='replace')
            self.fail('Regression timed out: '+relative)
        self.assertGreaterEqual(proc.returncode, 0, text.decode(errors='replace'))
        return text.decode(errors='replace')

    def test_unknown_calls_do_not_produce_false_safe_summary(self):
        text = self.run_case('loops/for_bounded_loop1.c')
        self.assertIn('FALSE(integer-relaxed)', text)
        self.assertNotIn('TRUE(integer-relaxed)', text)

    def test_zero_iteration_exit_keeps_count_constraints(self):
        text = self.run_case('nla-digbench/cohencu-ll__assert03.c')
        self.assertIn('TRUE(integer-relaxed)', text)

    def test_phase_count_matrix_does_not_abort(self):
        text = self.run_case('loop-acceleration/phases_1-1.c')
        self.assertIn('TRUE(integer-relaxed)', text)

    def test_nondeterministic_guard_does_not_abort(self):
        text = self.run_case('loop-invariants/eq1.c', allow_timeout=True)
        self.assertIn('[oopsla23] Rejecting loop with unknown per-iteration calls', text)


if __name__ == '__main__':
    unittest.main()
