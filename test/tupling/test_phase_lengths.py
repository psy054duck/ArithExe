from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'rec_solver'))

from rec_solver import solve_str
from rec_solver.rec_parser import parse_str
from rec_solver.core.phase_lengths import PhaseLengthBudget, PhaseLengthOptions, PhasePlan, phase_length_scope
from rec_solver.core import ultimately_periodic as periodic
from rec_solver.core.verification import is_inductive_scalar_closed_form
from rec_solver.core.closed_form import ExprClosedForm


class PhaseLengthTests(unittest.TestCase):
    def bounded(self):
        return parse_str('''
i(0)=p; s(0)=u;
if (And(i(n)>=0,i(n)<3)) { i(n+1)=i(n)+1; s(n+1)=s(n)+2; }
else { i(n+1)=i(n)+1; s(n+1)=s(n); }
''')

    def reset(self):
        return parse_str('''
i(0)=0; s(0)=u;
if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=0; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+1; }
''')

    def test_singleton_is_proved_and_infinite_suffix_is_not_bounded(self):
        budget = PhaseLengthBudget()
        plans = budget.plans(self.reset(), [([0], 1), ([1], 19)], z3.BoolVal(True))
        self.assertEqual(plans, [PhasePlan((1,), (1,))])
        self.assertEqual(budget.stats['checks'], 1)
        self.assertEqual(budget.stats['proved_bounds'], 1)

    def test_at_most_three_enumerates_shorter_counts(self):
        budget = PhaseLengthBudget()
        plans = budget.plans(self.bounded(), [([0], 3), ([1], 17)], z3.BoolVal(True))
        self.assertEqual([plan.counts for plan in plans], [(1,), (2,), (3,)])
        self.assertEqual([plan.bounds for plan in plans], [(3,)]*3)
        self.assertEqual(budget.stats['fixed_counts'], 0)

    def test_sample_count_alone_is_not_a_bound(self):
        rec = parse_str('i(0)=0; if (i(n)<N) { i(n+1)=i(n)+1; } else { i(n+1)=i(n); }')
        budget = PhaseLengthBudget()
        plans = budget.plans(rec, [([0], 2), ([1], 18)], z3.BoolVal(True))
        self.assertIsNone(plans[0].bounds[0])
        self.assertFalse(isinstance(plans[0].counts[0], int))

    def test_count_variables_do_not_capture_input_parameters(self):
        rec = parse_str('i(0)=0; if (i(n)<_phase_count0) { i(n+1)=i(n)+1; } else { i(n+1)=i(n); }')
        plan = PhaseLengthBudget().plans(rec, [([0], 2), ([1], 18)], z3.BoolVal(True))[0]
        self.assertNotEqual(str(plan.counts[0]), '_phase_count0')

    def test_whole_multibranch_component_is_unrolled(self):
        rec = parse_str('''
i(0)=0;
if (And(i(n)>=0,i(n)<4,i(n)%2==0)) { i(n+1)=i(n)+1; }
else if (And(i(n)>=0,i(n)<4,i(n)%2==1)) { i(n+1)=i(n)+1; }
else { i(n+1)=i(n)+1; }
''')
        budget = PhaseLengthBudget()
        self.assertIsNone(budget.upper_bound(rec, [0, 1], 1, z3.BoolVal(True)))
        self.assertEqual(budget.upper_bound(rec, [0, 1], 2, z3.BoolVal(True)), 2)
        limited = PhaseLengthBudget(dict(max_transitions=5))
        self.assertIsNone(limited.upper_bound(rec, [0, 1], 2, z3.BoolVal(True)))
        self.assertEqual(limited.stats['checks'], 0)

    def test_simultaneous_assignments_are_not_sequential(self):
        rec = parse_str('''
x(0)=p; y(0)=q;
if (x(n)==0) { x(n+1)=y(n); y(n+1)=1; }
else { x(n+1)=x(n); y(n+1)=y(n); }
''')
        budget = PhaseLengthBudget()
        self.assertIsNone(budget.upper_bound(rec, [0], 1, z3.BoolVal(True)))
        self.assertEqual(budget.upper_bound(rec, [0], 2, z3.BoolVal(True)), 2)

    def test_explicit_iteration_in_guards_advances_during_unrolling(self):
        rec = parse_str('i(0)=p; if (n%2==0) { i(n+1)=i(n)+1; } else { i(n+1)=i(n); }')
        self.assertEqual(PhaseLengthBudget().upper_bound(rec, [0], 1, z3.BoolVal(True)), 1)

    def test_initial_state_is_not_assumed_at_arbitrary_phase_entry(self):
        rec = parse_str('i(0)=0; if (i(n)<3) { i(n+1)=i(n)+1; } else { i(n+1)=i(n); }')
        initial_only = next(iter(rec.all_funcs)) == 0
        self.assertIsNone(PhaseLengthBudget().upper_bound(rec, [0], 3, initial_only))

    def test_parameter_preconditions_are_used_and_cache_is_region_specific(self):
        rec = parse_str('''
i(0)=0;
if (And(i(n)>=0,i(n)<N)) { i(n+1)=i(n)+1; }
else { i(n+1)=i(n)+1; }
''')
        budget = PhaseLengthBudget()
        self.assertEqual(budget.upper_bound(rec, [0], 3, z3.Int('N')<=3), 3)
        self.assertIsNone(budget.upper_bound(rec, [0], 3, z3.BoolVal(True)))
        self.assertEqual(budget.stats['checks'], 2)

    def test_unknown_never_proves_a_bound_and_is_cached(self):
        budget = PhaseLengthBudget()
        rec = self.reset()
        with patch('rec_solver.core.phase_lengths.z3.Solver') as solver:
            solver.return_value.check.return_value = z3.unknown
            self.assertIsNone(budget.upper_bound(rec, [0], 1, z3.BoolVal(True)))
            self.assertIsNone(budget.upper_bound(rec, [0], 1, z3.BoolVal(True)))
        self.assertEqual(budget.stats['unknown_checks'], 1)
        self.assertEqual(budget.stats['cache_hits'], 1)

    def test_one_shared_time_budget_covers_multiple_templates(self):
        budget = PhaseLengthBudget(dict(total_time_ms=1))
        budget.stats['preprocessing_seconds'] = 0.001
        for _ in range(2):
            plans = budget.plans(self.reset(), [([0], 1), ([1], 19)], z3.BoolVal(True))
            self.assertFalse(plans[0].specialized)
        self.assertEqual(budget.stats['checks'], 0)

    def test_query_timeout_is_clamped_to_remaining_budget(self):
        budget = PhaseLengthBudget(dict(total_time_ms=20))
        with patch('rec_solver.core.phase_lengths.z3.Solver') as solver:
            solver.return_value.check.return_value = z3.unknown
            budget.upper_bound(self.reset(), [0], 1, z3.BoolVal(True))
        self.assertGreater(solver.return_value.set.call_args.kwargs['timeout'], 0)
        self.assertLessEqual(solver.return_value.set.call_args.kwargs['timeout'], 20)

    def test_enumeration_caps_retain_unresolved_variables_and_proved_bounds(self):
        budget = PhaseLengthBudget(dict(max_variants=4))
        pattern = [([0], 2)]*4 + [([1], 99)]
        with patch.object(budget, 'upper_bound', return_value=2):
            plans = budget.plans(self.bounded(), pattern, z3.BoolVal(True))
        self.assertEqual(len(plans), 4)
        self.assertEqual(plans[0].bounds, (2, 2, 2, 2))
        self.assertFalse(isinstance(plans[0].counts[2], int))
        limited = PhaseLengthBudget(dict(max_enum_count=2))
        plans = limited.plans(self.bounded(), [([0], 3), ([1], 17)], z3.BoolVal(True))
        self.assertEqual(plans[0].bounds, (3,))
        self.assertFalse(isinstance(plans[0].counts[0], int))

    def test_reverse_composition_falls_back_without_guard_proofs(self):
        rec = self.reset()
        rec.set_reverse(set(rec.all_funcs))
        budget = PhaseLengthBudget()
        self.assertFalse(budget.plans(rec, [([0], 1), ([1], 19)], z3.BoolVal(True))[0].specialized)
        self.assertEqual(budget.stats['checks'], 0)

    def test_scope_is_inherited_and_reset_on_failure(self):
        stats = {}
        with self.assertRaises(RuntimeError):
            with phase_length_scope(stats=stats) as outer:
                with phase_length_scope() as inner:
                    self.assertIs(inner, outer)
                    inner.stats['checks'] = 4
                raise RuntimeError('failed request')
        self.assertEqual(stats['checks'], 4)
        with phase_length_scope() as fresh:
            self.assertEqual(fresh.stats['checks'], 0)

    def test_invalid_options_and_disabled_path(self):
        for options in [dict(total_time_ms=-1), dict(check_timeout_ms=0), dict(max_variants=0)]:
            with self.assertRaises(ValueError):
                PhaseLengthOptions(**options)
        budget = PhaseLengthBudget(dict(enabled=False))
        self.assertFalse(budget.plans(self.reset(), [([0], 1), ([1], 19)], z3.BoolVal(True))[0].specialized)
        self.assertEqual(budget.stats['checks'], 0)

    def test_fixed_phase_is_instantiated_and_unused_quantifiers_disappear(self):
        rec = parse_str('i(0)=p; if (true) { i(n+1)=i(n)+1; }')
        pattern = [([0], 1), ([0], 19)]
        candidate = periodic._compute_solution_by_index_seq(rec, pattern)
        formula, variables = periodic._set_up_constraints(rec, candidate, pattern)
        self.assertEqual(variables, [])
        self.assertTrue(z3.is_true(z3.simplify(formula)))
        with patch.object(periodic.z3, 'Then', side_effect=AssertionError('unnecessary QE')), \
                patch.object(periodic.utils, 'solve_piecewise_sol', side_effect=AssertionError('no counts')):
            results = periodic._solve_phase_template(rec, pattern, PhasePlan((1,), (1,)),
                z3.BoolVal(True), True, PhaseLengthBudget())
        self.assertEqual(len(results), 1)
        self.assertTrue(is_inductive_scalar_closed_form(rec, results[0][1], require_complete=True))

    def test_multiple_residues_use_one_relative_cycle_variable(self):
        rec = parse_str('''
i(0)=0;
if (And(i(n)%2==0,i(n)<N)) { i(n+1)=i(n)+1; }
else if (i(n)<N) { i(n+1)=i(n)+1; }
else { i(n+1)=i(n)+1; }
''')
        q = z3.Int('q')
        pattern = [([0, 1], q), ([2], 1)]
        candidate = periodic._compute_solution_by_index_seq(rec, pattern)
        formula, variables = periodic._set_up_constraints(rec, candidate, pattern)
        self.assertEqual(len(variables), 2)  # one per phase, not one per residue
        solver = z3.Solver()
        solver.add(q>=1, z3.ForAll(variables, z3.Implies(z3.And(*[k>=0 for k in variables]), formula)),
                   z3.Int('N') != 2*q)
        self.assertEqual(solver.check(), z3.unsat)

    def test_shorter_counts_remain_solvable_for_all_initial_states(self):
        source = '''
i(0)=p; s(0)=u;
if (And(i(n)>=0,i(n)<3)) { i(n+1)=i(n)+1; s(n+1)=s(n)+2; }
else { i(n+1)=i(n)+1; s(n+1)=s(n); }
'''
        stats = {}
        result = solve_str(source, phase_length_stats=stats)
        self.assertEqual(len(result.as_dict()), 2)
        self.assertTrue(is_inductive_scalar_closed_form(parse_str(source), result,
                                                      timeout_ms=5000, require_complete=True))
        self.assertGreater(stats['proved_bounds'], 0)

    def test_invalid_specialization_cannot_bypass_checks_via_general_fallback(self):
        rec = self.reset()
        bad = ExprClosedForm({decl(rec.ind_var): z3.IntVal(0) for decl in rec.func_decls}, rec.ind_var)
        stats = {}
        with patch.object(periodic, '_try_fixed_symbolic_pattern', return_value=None), \
                patch.object(periodic, '_infer_phase_template', return_value=[]), \
                patch.object(periodic, '_solve_phase_template', return_value=[(z3.BoolVal(True), bad)]):
            with self.assertRaises(periodic.UnsolvableError):
                periodic.solve_ultimately_periodic_symbolic(rec, phase_length_stats=stats)
        self.assertEqual(stats['rejected_candidates'], 2)

    def test_inconclusive_candidate_certificate_rejects_specialization(self):
        rec = self.reset()
        with patch.object(periodic, '_try_fixed_symbolic_pattern', return_value=None), \
                patch.object(periodic, '_infer_phase_template', return_value=[]), \
                patch.object(periodic, 'is_inductive_scalar_closed_form', return_value=False):
            with self.assertRaises(periodic.UnsolvableError):
                periodic.solve_ultimately_periodic_symbolic(rec)

    def test_symbolic_array_boundary_is_solved_and_certified(self):
        source = '''
i(0)=0; s(0)=u;
if (i(n)==0) { i(n+1)=i(n)+1; s(n+1)=0; }
else if (And(i(n)>=0,i(n)<=limit_value-2)) { i(n+1)=i(n)+1; s(n+1)=s(n)+i(n)%5; }
else { i(n+1)=i(n)+1; s(n+1)=s(n)+(limit_value+4)%5; }
'''
        stats = {}
        closed = solve_str(source, phase_length_stats=stats)
        self.assertEqual(len(closed.as_dict()), 2)
        self.assertTrue(is_inductive_scalar_closed_form(parse_str(source), closed,
                                                      timeout_ms=5000, require_complete=True))
        n, N = z3.Ints('n limit_value')
        actual = next(rhs for lhs, rhs in closed.as_dict().items() if lhs.decl().name() == 's')
        r = N%5
        solver = z3.Solver()
        solver.set(timeout=5000)
        solver.add(N>0, z3.substitute(actual, (n, N)) != 10*(N/5)+(r*(r-1))/2)
        self.assertEqual(solver.check(), z3.unsat)
        self.assertGreater(stats['fixed_counts'], 0)


if __name__ == '__main__':
    unittest.main()
