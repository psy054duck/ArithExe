from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "rec_solver"))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core.modulo_normalization import modulo_branch_limit, normalize_constant_modulo
from rec_solver.core.ultimately_periodic import solve_ultimately_periodic_symbolic
from rec_solver.core import utils


class ModuloNormalizationTests(unittest.TestCase):
    def assert_equivalent(self, original, normalized):
        solver = z3.Solver()
        solver.set(timeout=3000)
        for old_guard, old_trans in zip(original.conditions, original.transitions):
            matches = []
            for guard, trans in zip(normalized.conditions, normalized.transitions):
                matches.append(z3.And(guard, *[trans[lhs] == rhs for lhs, rhs in old_trans.items()]))
            solver.push()
            solver.add(old_guard, z3.Not(z3.Or(*matches)))
            self.assertEqual(solver.check(), z3.unsat)
            solver.pop()
        for guard, trans in zip(normalized.conditions, normalized.transitions):
            matches = [z3.And(old_guard, *[old_trans[lhs] == rhs for lhs, rhs in trans.items()])
                       for old_guard, old_trans in zip(original.conditions, original.transitions)]
            solver.push()
            solver.add(guard, z3.Not(z3.Or(*matches)))
            self.assertEqual(solver.check(), z3.unsat)
            solver.pop()

    def test_shared_residue_is_substituted_in_all_updates(self):
        original = parse_str('''
i(0)=0; s(0)=u;
if (true) { i(n+1)=i(n)+(i(n)%3); s(n+1)=s(n)+2*(i(n)%3); }
''')
        result = normalize_constant_modulo(original)
        self.assertEqual(len(result.conditions), 3)
        self.assert_equivalent(original, result)
        self.assertTrue(any('mod' in rhs.sexpr() for trans in original.transitions for rhs in trans.values()))
        self.assertFalse(any('mod' in rhs.sexpr() for trans in result.transitions for rhs in trans.values()))

    def test_nested_and_correlated_modulo(self):
        original = parse_str('''
i(0)=p; s(0)=u;
if (true) { i(n+1)=i(n)+1;
 s(n+1)=s(n)+(i(n)%3)%2+(i(n)+3)%3; }
''')
        result = normalize_constant_modulo(original)
        self.assertEqual(len(result.conditions), 3)
        self.assert_equivalent(original, result)

    def test_infeasible_residues_are_pruned_under_original_guard(self):
        original = parse_str('''
i(0)=0; s(0)=u;
if (i(n)%2==0) { i(n+1)=i(n)+1; s(n+1)=s(n)+i(n)%2; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+1; }
''')
        result = normalize_constant_modulo(original)
        self.assertEqual(len(result.conditions), 2)
        self.assert_equivalent(original, result)

    def test_negative_numerators_and_divisors_preserve_integer_modulo(self):
        original = parse_str('''
i(0)=p; s(0)=u;
if (true) { i(n+1)=i(n)-1; s(n+1)=s(n)+(-2*i(n)+1)%(-3); }
''')
        result = normalize_constant_modulo(original)
        self.assertEqual(len(result.conditions), 3)
        self.assert_equivalent(original, result)

    def test_modulus_one_simplifies_without_extra_branches(self):
        original = parse_str('i(0)=0; if (true) { i(n+1)=i(n)+1+i(n)%1; }')
        result = normalize_constant_modulo(original)
        self.assertEqual(len(result.conditions), 1)
        self.assert_equivalent(original, result)

    def test_guard_only_and_initial_modulo_are_not_expanded(self):
        original = parse_str('i(0)=p%3; if (i(n)%2==0) { i(n+1)=i(n)+1; }')
        self.assertIs(normalize_constant_modulo(original), original)

    def test_symbolic_or_zero_divisor_is_not_expanded(self):
        for divisor in ['p', '0']:
            original = parse_str(f'i(0)=0; if (true) {{ i(n+1)=i(n)+i(n)%{divisor}; }}')
            self.assertIs(normalize_constant_modulo(original), original)

    def test_limit_rolls_back_all_branches_and_zero_disables(self):
        original = parse_str('''
i(0)=0; s(0)=0;
if (true) { i(n+1)=i(n)+1; s(n+1)=s(n)+i(n)%3+s(n)%3; }
''')
        self.assertIs(normalize_constant_modulo(original, max_branches=4), original)
        self.assertIs(normalize_constant_modulo(original, max_branches=0), original)
        result = normalize_constant_modulo(original, max_branches=9)
        self.assertEqual(len(result.conditions), 9)
        self.assert_equivalent(original, result)
        with self.assertRaises(ValueError):
            normalize_constant_modulo(original, max_branches=-1)

    def test_unknown_feasibility_retains_every_residue(self):
        original = parse_str('i(0)=0; if (true) { i(n+1)=i(n)+i(n)%3; }')
        with patch('rec_solver.core.modulo_normalization.z3.Solver') as solver:
            solver.return_value.check.return_value = z3.unknown
            result = normalize_constant_modulo(original)
        self.assertEqual(len(result.conditions), 3)
        self.assert_equivalent(original, result)

    def test_branch_limit_is_scoped_and_restored_after_failure(self):
        original = parse_str('i(0)=0; if (true) { i(n+1)=i(n)+i(n)%3; }')
        with self.assertRaisesRegex(RuntimeError, 'scope failure'):
            with modulo_branch_limit(0):
                self.assertIs(normalize_constant_modulo(original), original)
                raise RuntimeError('scope failure')
        self.assertEqual(len(normalize_constant_modulo(original).conditions), 3)

    def test_reverse_composition_and_partial_metadata_are_preserved(self):
        original = parse_str('i(0)=0; if (true) { i(n+1)=i(n)+i(n)%2; }')
        original.set_reverse(set(original.all_funcs))
        original._closed_forms = {z3.Int('known'): z3.IntVal(2)}
        result = normalize_constant_modulo(original)
        self.assertEqual(result.reverses, original.reverses)
        self.assertEqual(result.closed_forms, original.closed_forms)
        self.assertIsNot(result.reverses, original.reverses)
        self.assertIsNot(result._closed_forms, original._closed_forms)
        self.assertEqual(result.initial, original.initial)

    def test_solver_and_internal_scalar_entry_points_solve_periodic_sum(self):
        for modulus in [2, 3, 4, 5]:
            source = f'''
i(0)=0; s(0)=u;
if (i(n)==0) {{ i(n+1)=i(n)+1; s(n+1)=0; }}
else {{ i(n+1)=i(n)+1; s(n+1)=s(n)+i(n)%{modulus}; }}
'''
            for backend in [lambda: solve_str(source),
                            lambda: solve_ultimately_periodic_symbolic(parse_str(source))]:
                with self.subTest(modulus=modulus, backend=backend):
                    result = backend().as_dict()
                    n, u = z3.Ints('n u')
                    q, r = n/modulus, n%modulus
                    expected = z3.If(n == 0, u, q*(modulus*(modulus-1)//2)+r*(r-1)/2)
                    actual = next(rhs for lhs, rhs in result.items() if lhs.decl().name() == 's')
                    solver = z3.Solver()
                    solver.set(timeout=5000)
                    solver.add(n >= 0, actual != expected)
                    self.assertEqual(solver.check(), z3.unsat)
                    self.assertEqual(len(result), 2)

    def test_periodic_suffix_rotation_preserves_the_entire_cycle(self):
        for period in range(2, 7):
            cycle = list(range(1, period+1))
            for offset in range(1, period):
                sequence = [0] + cycle[-offset:] + cycle*6
                pattern = utils.compress_seq(sequence)
                self.assertEqual(len(pattern[-1][0]), period)
                expanded = utils.flatten_seq(pattern[:-1]) + pattern[-1][0]*8
                self.assertEqual(expanded[:len(sequence)], sequence)


if __name__ == '__main__':
    unittest.main()
