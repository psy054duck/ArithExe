from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'rec_solver'))

from rec_solver.rec_parser import parse_str
from rec_solver.core.dag_reconstruction import solve_nearly_tail
from rec_solver.core.guard_phase_lengths import infer_guard_phase_lengths, uncovered_region
from rec_solver.core.phase_lengths import PhaseLengthBudget, PhasePlan, phase_length_scope
from rec_solver.core import ultimately_periodic as periodic
from rec_solver.core.closed_form import ExprClosedForm
from rec_solver.core.verification import is_inductive_scalar_closed_form


class GuardPhaseLengths(unittest.TestCase):
    def infer(self, guard, suffix, precondition=z3.BoolVal(True), **options):
        r, q = z3.Ints('r q')
        budget = PhaseLengthBudget(options)
        result = infer_guard_phase_lengths([(r, guard), (r, suffix)],
                                          (q,), (1,), (None,), precondition, budget)
        for region, mapping in result:
            value = mapping[q]
            solver = z3.Solver()
            solver.add(region, z3.Or(value < 1,
                       z3.And(r >= 0, r < value, z3.Not(guard)),
                       z3.And(r >= 0, z3.Not(z3.substitute(suffix, list(mapping.items()))))))
            self.assertEqual(solver.check(), z3.unsat)
        return result, budget

    def test_floor_boundary_avoids_enumerating_large_stride(self):
        r, k, t, q = z3.Ints('r k t q')
        guard = z3.And(t-r > 0, z3.Not(z3.And(1500*(t-r-1) <= k, k < 1500*(t-r))))
        suffix = z3.Or(t-q <= 0, z3.And(1500*(t-q-1) <= k, k < 1500*(t-q)))
        precondition = z3.And(k >= 0, t > 0, z3.Not(z3.And(1500*(t-1) <= k, k < 1500*t)))
        results, budget = self.infer(guard, suffix, precondition)
        self.assertGreater(len(results), 0)
        self.assertTrue(z3.is_false(uncovered_region(precondition, [r for r, _ in results], budget)))

    def test_nonconvex_guard_cannot_be_certified_by_endpoints_alone(self):
        r = z3.Int('r')
        results, _ = self.infer(z3.Or(r == 0, r == 2), z3.BoolVal(True))
        self.assertTrue(results)
        for _, mapping in results:
            self.assertTrue(z3.is_true(z3.simplify(mapping[z3.Int('q')] == 1)))

    def test_unknown_certification_is_not_accepted(self):
        r, n = z3.Ints('r n')
        with patch('rec_solver.core.guard_phase_lengths._check', return_value=z3.unknown):
            result, _ = self.infer(r < n, z3.BoolVal(True), n > 0)
        self.assertEqual(result, [])

    def test_disabled_and_exhausted_budget_fall_back(self):
        r, q = z3.Ints('r q')
        for options in (dict(enabled=False), dict(total_time_ms=0)):
            budget = PhaseLengthBudget(options)
            self.assertEqual(infer_guard_phase_lengths(
                [(r, r < 5), (r, z3.BoolVal(True))], (q,), (1,), (None,),
                z3.BoolVal(True), budget), [])

    def test_nonlinear_boundaries_are_not_guessed_from_linear_samples(self):
        r, n = z3.Ints('r n')
        result, _ = self.infer(r*r < n, z3.BoolVal(True), n > 2)
        self.assertEqual(result, [])

    def test_infinite_suffix_is_checked_beyond_its_first_iteration(self):
        r = z3.Int('r')
        result, _ = self.infer(r < 3, r < 100)
        self.assertEqual(result, [])

    def test_earlier_counts_are_substituted_into_later_guards(self):
        r, x, y, n, m = z3.Ints('r x y n m')
        budget = PhaseLengthBudget()
        result = infer_guard_phase_lengths(
            [(r, r < n), (r, r+x < m), (r, z3.BoolVal(True))],
            (x, y), (1, 1), (None, None), z3.And(n > 0, m > n), budget)
        self.assertTrue(result)
        for region, mapping in result:
            solver = z3.Solver()
            solver.add(region, z3.Or(mapping[x] != n, mapping[y] != m-n))
            self.assertEqual(solver.check(), z3.unsat)

    def test_partial_region_keeps_uncovered_inputs_for_fallback(self):
        n = z3.Int('n')
        budget = PhaseLengthBudget()
        remaining = uncovered_region(z3.BoolVal(True), [n >= 0], budget)
        solver = z3.Solver()
        solver.add(remaining != (n < 0))
        self.assertEqual(solver.check(), z3.unsat)

    def test_reverse_guard_or_forward_dependency_is_not_supported(self):
        for condition, update in [('a(n)>0', 'x(n)+1'), ('x(n)>0', 'a(n)')]:
            rec = parse_str(f'''x(0)=p;a(0)=u;
                if({condition}){{x(n+1)={update};a(n+1)=a(n)+1;}}
                else{{x(n+1)=x(n);a(n+1)=a(n);}}''')
            reverse = next(decl for decl in rec.func_decls if decl.name() == 'a')
            rec.set_reverse({reverse(rec.ind_var+1)})
            with patch.object(periodic, '_compute_solution_by_index_seq',
                              side_effect=AssertionError('unsupported reverse dependency')):
                self.assertEqual(periodic._infer_phase_template(
                    rec, [([0], 1), ([1], 19)], PhasePlan((z3.Int('q'),), (None,)),
                    z3.BoolVal(True), True, PhaseLengthBudget()), [])

    def test_rejected_closed_form_keeps_original_fallback(self):
        rec = parse_str('i(0)=0;if(i(n)<limit_value){i(n+1)=i(n)+1;}else{i(n+1)=i(n);}')
        bad = ExprClosedForm({decl(rec.ind_var):z3.IntVal(0) for decl in rec.func_decls}, rec.ind_var)
        stats = {}
        with patch.object(periodic, '_try_fixed_symbolic_pattern', return_value=None), \
                patch.object(periodic, '_infer_phase_template', return_value=[(z3.BoolVal(True), bad)]), \
                patch.object(periodic, '_solve_phase_template', wraps=periodic._solve_phase_template) as fallback:
            result = periodic.solve_ultimately_periodic_symbolic(rec, phase_length_stats=stats)
        self.assertTrue(fallback.called)
        self.assertGreater(stats['rejected_candidates'], 0)
        self.assertTrue(is_inductive_scalar_closed_form(rec, result, require_complete=True))

    def test_multiple_residue_guards_are_certified_together(self):
        rec = parse_str('''i(0)=0;
            if(And(i(n)<N,i(n)%2==0)){i(n+1)=i(n)+1;}
            else if(i(n)<N){i(n+1)=i(n)+1;}
            else{i(n+1)=i(n);}''')
        precondition = z3.And(z3.Int('N') >= 2, z3.Int('N')%2 == 0)
        budget = PhaseLengthBudget()
        result = periodic._infer_phase_template(rec, [([0,1],1),([2],19)],
            PhasePlan((z3.Int('q'),),(None,)), precondition, True, budget)
        self.assertTrue(result)
        for condition, candidate in result:
            self.assertTrue(is_inductive_scalar_closed_form(
                rec, candidate, precondition=condition, require_complete=True))

    def test_custom_z3_context_and_negative_parameters(self):
        ctx = z3.Context()
        r, q, n = z3.Ints('r q n', ctx=ctx)
        budget = PhaseLengthBudget()
        result = infer_guard_phase_lengths(
            [(r, 3*r+n < 0), (r, z3.BoolVal(True,ctx=ctx))],
            (q,), (1,), (None,), n < 0, budget)
        self.assertTrue(result)
        self.assertTrue(z3.is_false(uncovered_region(n < 0, [c for c,_ in result], budget)))

    def test_complete_large_stride_recurrence_uses_no_phase_qe(self):
        rec = parse_str('''if(Or(t<=0,k<0)){f(k,t)=input(k);}
            else if(And(1500*(t-1)<=k,k<1500*t)){f(k,t)=c;}
            else{f(k,t)=f(k,t-1);}''')
        k, t = rec.func_sig.children()
        entry = z3.Function('input', z3.IntSort(), z3.IntSort())
        stats = {}
        with phase_length_scope(stats=stats):
            result, = solve_nearly_tail(rec)
        solver = z3.Solver()
        solver.add(result != z3.If(z3.And(t > 0, k >= 0, k < 1500*t), z3.Int('c'), entry(k)))
        self.assertEqual(solver.check(), z3.unsat)
        self.assertGreater(stats['boundary_solutions'], 0)
        self.assertEqual(stats['qe_calls'], 0)

    def test_large_stride_preserves_terminal_arguments(self):
        rec = parse_str('''if(Or(t<=0,k<0)){f(k,t)=input(k);}
            else if(And(1500*(t-1)<=k,k<1500*t)){f(k,t)=2*(t-1)+k;}
            else{f(k,t)=f(k,t-1);}''')
        k, t = rec.func_sig.children()
        entry = z3.Function('input', z3.IntSort(), z3.IntSort())
        stats = {}
        with phase_length_scope(stats=stats):
            actual, = solve_nearly_tail(rec)
        solver = z3.Solver()
        solver.set(timeout=5000)
        solver.add(actual != z3.If(z3.And(k >= 0, k < 1500*t), 2*(k/1500)+k, entry(k)))
        self.assertEqual(solver.check(), z3.unsat)
        self.assertEqual(stats['qe_calls'], 0)


if __name__ == '__main__':
    unittest.main()
