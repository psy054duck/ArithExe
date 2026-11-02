import sympy as sp
from sympy.core.function import AppliedUndef
import logging
import time
import z3
from functools import reduce
from itertools import product

from . import utils
from .recurrence import Recurrence, LoopRecurrence
from .closed_form import PeriodicClosedForm, PiecewiseClosedForm, SymbolicClosedForm
from .solvable_polynomial import solve_solvable_map, is_solvable_map
from .bounded_cfinite import can_attempt_bounded_cfinite_map, solve_bounded_cfinite_map, BoundedCFiniteError
from .logic_simplification import DNFConverter, my_simplify
from .verification import is_inductive_scalar_closed_form
from .modulo_normalization import normalize_constant_modulo
from .phase_lengths import PhasePlan, count_variables, phase_length_scope
from .guard_phase_lengths import infer_guard_phase_lengths, uncovered_region

logger = logging.getLogger(__name__)

class UnsolvableError(Exception):
    pass

def solve_ultimately_periodic_symbolic(
    rec: LoopRecurrence,
    bnd=100,
    precondition=z3.BoolVal(True),
    enable_bounded_cfinite=True,
    modulo_max_branches=None,
    phase_length_options=None,
    phase_length_stats=None,
):
    with phase_length_scope(phase_length_options, phase_length_stats) as budget:
        return _solve_ultimately_periodic_symbolic(
            rec, bnd, precondition, enable_bounded_cfinite, modulo_max_branches, budget)


def _solve_ultimately_periodic_symbolic(rec, bnd, precondition, enable_bounded_cfinite,
                                      modulo_max_branches, budget):
    original_rec = rec
    rec = normalize_constant_modulo(rec, max_branches=modulo_max_branches)
    z3_solver = z3.Solver()
    z3_solver.add(precondition)
    acc_condition = z3.BoolVal(True)
    i = 0
    logger.debug("Solving recurrence %s" % rec)
    # rec.pprint()
    constraints = []
    closed_forms = []
    if rec.is_all_initialized():
        return solve_ultimately_periodic_initial(
            rec,
            bnd,
            enable_bounded_cfinite=enable_bounded_cfinite,
        )
    while z3_solver.check(acc_condition) != z3.unsat:
        # print(acc_condition)
        i += 1
        model = z3_solver.model()
        parameters = rec.get_symbolic_values()
        cur_val = {p: model.eval(p, model_completion=True) for p in parameters}
        # cur_val = {z3.Int('x'): z3.IntVal(4), z3.Int('_ret0'): z3.IntVal(0)}
        initialized_rec = rec.subs(cur_val)
        # initialized_rec.pprint()
        # print(initialized_rec.conditions)
        _, index_seq = _solve_ultimately_periodic_initial(
            initialized_rec,
            bnd,
            enable_bounded_cfinite=enable_bounded_cfinite,
        )
        region = z3.simplify(z3.And(precondition, acc_condition))
        fixed_candidate = _try_fixed_symbolic_pattern(rec, index_seq, region, enable_bounded_cfinite)
        if fixed_candidate is not None:
            constraints.append(region)
            closed_forms.append(fixed_candidate)
            break
        plans = budget.plans(rec, index_seq, precondition)
        accepted = []
        rejected = False
        def certified(condition, candidate):
            if not budget.options.enabled or rec.reverses:
                return True
            budget.stats['candidate_checks'] += 1
            valid = is_inductive_scalar_closed_form(
                original_rec, candidate, precondition=z3.And(precondition, condition),
                timeout_ms=1000, require_complete=True)
            if not valid:
                budget.stats['rejected_candidates'] += 1
            return valid
        for plan in plans:
            inferred = _infer_phase_template(
                rec, index_seq, plan, region, enable_bounded_cfinite, budget)
            proved = []
            accelerated = []
            for condition, candidate in inferred:
                if certified(condition, candidate):
                    accelerated.append((condition, candidate))
                    proved.append(condition)
            # A path template describes a parameter region, not necessarily
            # the whole remaining space. The outer loop samples uncovered
            # regions separately; do not run QE for a template already proved
            # to cover its sample.
            if proved:
                sampled = z3.simplify(z3.substitute(z3.Or(*proved), list(cur_val.items())))
                sample_check = z3.Solver(ctx=rec.ind_var.ctx)
                sample_check.set(timeout=100)
                if sample_check.check(z3.Not(sampled)) == z3.unsat:
                    accepted.extend(accelerated)
                    budget.stats['boundary_solutions'] += len(accelerated)
                    break
            remaining = uncovered_region(region, proved, budget)
            if z3.is_false(remaining):
                accepted.extend(accelerated)
                budget.stats['boundary_solutions'] += len(accelerated)
                continue
            for condition, candidate in _solve_phase_template(
                    rec, index_seq, plan, precondition, enable_bounded_cfinite, budget):
                if certified(condition, candidate):
                    accepted.append((condition, candidate))
                else:
                    rejected |= plan.specialized
        if rejected:
            # A proof timeout must not turn the optimization into a rejection
            # of inputs previously handled by the general symbolic algorithm.
            general = PhasePlan(count_variables(rec, len(index_seq)-1, precondition),
                                (None,)*(len(index_seq)-1))
            for condition, candidate in _solve_phase_template(
                    rec, index_seq, general, precondition, enable_bounded_cfinite, budget):
                if certified(condition, candidate):
                    accepted.append((condition, candidate))
        if not accepted:
            raise UnsolvableError('No feasible parameter region for the sampled phase template.')
        coverage = z3.Or(*[condition for condition, _ in accepted])
        sampled_coverage = z3.simplify(z3.substitute(coverage, list(cur_val.items())))
        progress = z3.Solver(ctx=rec.ind_var.ctx)
        if progress.check(z3.Not(sampled_coverage)) != z3.unsat:
            raise UnsolvableError('Phase inference did not cover the sampled parameters.')
        for condition, candidate in accepted:
            constraints.append(condition)
            acc_condition = z3.And(acc_condition, z3.Not(condition))
            closed_forms.append(candidate)
    res = SymbolicClosedForm(constraints, closed_forms, rec.ind_var)
    return res


def _infer_phase_template(rec, pattern, plan, precondition, enable_bounded_cfinite, budget):
    if (not budget.options.enabled or not plan.counts or plan.specialized
            or budget.remaining_ms < 1
            or sum(len(seq) for seq, _ in pattern) > budget.options.max_transitions):
        return []
    reverse_declarations = {app.decl() for app in rec.reverses}
    guard_inputs = set().union(*[utils.get_applied_functions(guard) for guard in rec.conditions])
    for transition in rec.transitions:
        for lhs, rhs in transition.items():
            if lhs.decl() not in reverse_declarations:
                guard_inputs.update(utils.get_applied_functions(rhs))
    if any(app.decl() in reverse_declarations for app in guard_inputs):
        return []
    with budget.processing():
        template = [(seq, count) for (seq, _), count in zip(pattern, plan.counts)] + [pattern[-1]]
        candidate = _compute_solution_by_index_seq(
            rec, template, enable_bounded_cfinite=enable_bounded_cfinite)
        guards = []
        acc = z3.IntVal(0, ctx=rec.ind_var.ctx)
        for component, (seq, count) in zip(candidate.closed_forms, template):
            cycle = z3.FreshInt('guard_cycle', ctx=rec.ind_var.ctx)
            residues = []
            for offset, branch in enumerate(seq):
                guard = z3.substitute(rec.conditions[branch], list(
                    component.get_rth_part_closed_form(offset).items()))
                residues.append(z3.substitute(
                    guard, (rec.ind_var, acc+len(seq)*cycle+offset)))
            guards.append((cycle, z3.simplify(z3.And(*residues))))
            acc += len(seq)*count
        inferred = infer_guard_phase_lengths(
            guards, plan.counts, [count for _, count in pattern[:-1]],
            plan.bounds, precondition, budget)
        return [(condition, candidate.simple_subs(mapping))
                for condition, mapping in inferred]


def _solve_phase_template(rec, pattern, plan, precondition, enable_bounded_cfinite, budget):
    template = [(seq, count) for (seq, _), count in zip(pattern, plan.counts)] + [pattern[-1]]
    variables = [count for count in plan.counts if not isinstance(count, int)]
    candidate = _compute_solution_by_index_seq(
        rec, template, enable_bounded_cfinite=enable_bounded_cfinite)
    formula, ks = _set_up_constraints(rec, candidate, template, budget=budget)
    bounds = [count >= 1 for count in variables]
    bounds.extend(count <= upper for count, upper in zip(plan.counts, plan.bounds)
                  if upper is not None and not isinstance(count, int))
    formula = z3.And(formula, *bounds)
    if ks:
        formula = z3.ForAll(ks, z3.Implies(z3.And(*[k >= 0 for k in ks]), formula))
        start = time.monotonic()
        formula = z3.Then('simplify', 'qe', 'simplify').apply(formula).as_expr()
        budget.stats['qe_calls'] += 1
        budget.stats['qe_seconds'] += time.monotonic()-start
    constraint = z3.simplify(z3.And(precondition, formula))
    if z3.is_false(constraint):
        return []
    if variables:
        solution = utils.solve_piecewise_sol(constraint, variables, sort=z3.Int,
                                            precondition=precondition)
        if solution is None:
            from .phase_lengths import solve_integer_phase_length
            solutions = (solve_integer_phase_length(constraint, variables[0])
                         if len(variables) == 1 else None)
            if solutions is None:
                raise UnsolvableError('Could not solve the remaining symbolic phase lengths.')
        else:
            solutions = zip(solution.conditions, solution.expressions)
    else:
        # With no unknown counts, this is already the parameter region. Do
        # not invoke an arithmetic synthesizer with an empty unknown vector.
        solutions = [(z3.BoolVal(True, ctx=rec.ind_var.ctx), {})]
    results = []
    for region, values in solutions:
        condition = z3.And(constraint, region)
        if values:
            condition = z3.substitute(condition, list(values.items()))
        condition = my_simplify(condition, assumption=precondition)
        if not z3.is_false(condition):
            results.append((condition, candidate.simple_subs(values)))
    return results


def _try_fixed_symbolic_pattern(rec, index_seq, precondition, enable_bounded_cfinite=True):
    if rec.reverses or any(decl.arity() != 1 for decl in rec.func_decls):
        return None
    try:
        candidate = _compute_solution_by_index_seq(
            rec, index_seq, enable_bounded_cfinite=enable_bounded_cfinite)
        # A sampled schedule is only a proposal; prove the original equations.
        if is_inductive_scalar_closed_form(rec, candidate, precondition=precondition,
                                          timeout_ms=1000, require_complete=True):
            return candidate
    except (UnsolvableError, z3.Z3Exception, ValueError, NotImplementedError):
        pass
    return None
    

# def _unpack_q_expressions(q_linear):
#     conditions = q_linear.conditions
#     expressions = q_linear.expressions
#     conditions = [conditional_e.conditions for conditional_e in q_linear]
#     expressions = [conditional_e.expressions for conditional_e in q_linear]
#     lengths = [len(conditional_e.conditions) for conditional_e in q_linear]
#     indices = [tuple(range(l)) for l in lengths]
#     for index in product(*indices):
#         yield z3.And(*[condition[i] for i, condition in zip(index, conditions)]), [expression[i] for i, expression in zip(index, expressions)]

def solve_ultimately_periodic_initial(rec: LoopRecurrence, bnd=100, enable_bounded_cfinite=True,
                                    modulo_max_branches=None):
    rec = normalize_constant_modulo(rec, max_branches=modulo_max_branches)
    closed_form, _ = _solve_ultimately_periodic_initial(
        rec,
        bnd,
        enable_bounded_cfinite=enable_bounded_cfinite,
    )
    return closed_form

def _solve_ultimately_periodic_initial(rec: LoopRecurrence, bnd=100, enable_bounded_cfinite=True):
    assert(rec.is_all_initialized())
    n = 10
    start = 0
    ith = 1
    closed_form = PiecewiseClosedForm(rec.ind_var)
    acc_index_seq = []
    start_value = rec.initial
    while n < bnd:
        n *= 2
        candidate, guessed_index_seq = _compute_candidate_solution(
            rec,
            start,
            n,
            ith,
            start_value=start_value,
            enable_bounded_cfinite=enable_bounded_cfinite,
        )
        shift_candidate = candidate.subs({candidate.ind_var: candidate.ind_var - start})
        smallest = verify(rec, shift_candidate, guessed_index_seq, start_offset=start)
        closed_form = closed_form.concatenate(shift_candidate, start)
        if smallest is None:
            acc_index_seq.extend(guessed_index_seq)
            # The final block denotes an infinite, verified suffix. Recompressing
            # its finite witness can absorb a transient into a different cycle.
            return closed_form, acc_index_seq
        if smallest <= start:
            raise UnsolvableError("Periodic candidate fails at its initial iteration.")
        acc_index_seq.extend(_index_seq_prefix(guessed_index_seq, smallest - start))
        start = smallest
        start_value = closed_form.eval_at(start).as_dict()
        start_value = {z3.substitute(k, (rec.ind_var, z3.IntVal(start))): v for k, v in start_value.items()}
        ith += 1
    raise UnsolvableError("No verified periodic candidate within the search bound.")

def _index_seq_prefix(pattern, length):
    prefix = []
    for index, (seq, count) in enumerate(pattern):
        take = length if index == len(pattern) - 1 else min(length, len(seq)*count)
        repeats, remainder = divmod(take, len(seq))
        if repeats:
            prefix.append((seq, repeats))
        if remainder:
            prefix.append((seq[:remainder], 1))
        length -= take
        if length == 0:
            break
    return prefix

def verify(rec: Recurrence, candidate_sol: PiecewiseClosedForm, pattern: list, start_offset=0):
    conditions = rec.conditions
    start = start_offset + sum(len(seq)*cnt for seq, cnt in pattern[:-1])
    last_closed_form = candidate_sol.closed_forms[-1]
    assert(isinstance(last_closed_form, PeriodicClosedForm))
    period = last_closed_form.period
    periodic_index_seq, _ = pattern[-1]
    k = z3.FreshInt('period', ctx=rec.ind_var.ctx)
    smallest = None
    for r, i in enumerate(periodic_index_seq):
        cond = conditions[i]
        if z3.is_true(cond):
            continue
        mapping = [(k, v if z3.is_int(v) else z3.ToInt(v)) for k, v in last_closed_form.get_rth_part_closed_form(r).items()]
        cond = z3.substitute(cond, mapping)
        # Residues are relative to the suffix start, not to iteration zero.
        cond = z3.substitute(cond, (candidate_sol.ind_var, start + period*k + r))
        k_range = k >= 0
        solver = z3.Solver()
        solver.add(k_range)
        solver.add(z3.Not(cond))
        result = solver.check()
        if result == z3.unknown:
            raise UnsolvableError("Periodic candidate verification was inconclusive.")
        if result == z3.sat:
            cur_smallest = _smallest_violation(k_range, cond, k)
            cur_smallest = z3.simplify(start + cur_smallest*period + r).as_long()
            if smallest is None:
                smallest = cur_smallest
            else:
                smallest = min(smallest, cur_smallest)
    return smallest

def _smallest_violation(n_range, cond_range, k):
    '''both n_range and cond_range are z3 expressions representing some certain intervals, where k is the intermediate variable'''
    minimal = z3.Int('_minimal')
    solver = z3.Solver()
    solver.add(z3.ForAll(k, z3.Implies(k < minimal, z3.Implies(n_range, cond_range))))
    solver.add(z3.Not(z3.substitute(z3.Implies(n_range, cond_range), (k, minimal))))
    sat_res = solver.check()
    assert(sat_res == z3.sat)
    m = solver.model()
    return m.eval(minimal)

def _compute_solution_by_index_seq(rec: LoopRecurrence, index_seq, enable_bounded_cfinite=True):
    patterns = [seq for seq, _ in index_seq]
    acc_thresholds = [sum(len(seq)*cnt for seq, cnt in index_seq[:i]) for i in range(1, len(index_seq))]
    # nums = [cnt for _, cnt in index_seq]
    thresholds = [0] + acc_thresholds + [sp.oo]
    nonconditional = [
        _solve_as_nonconditional(
            rec,
            pattern,
            enable_bounded_cfinite=enable_bounded_cfinite,
        )
        for pattern in patterns
    ]
    shift_closed = [closed.subs({closed.ind_var: closed.ind_var - shift}) for closed, shift in zip(nonconditional, thresholds)]
    closed_forms = []
    initial = rec.initial
    acc = 0
    for closed, (seq, t) in zip(shift_closed, index_seq):
        acc += t*len(seq)
        closed_forms.append(closed.subs(initial, rec.reverses))
        initial = {k.decl()(0): v for k, v in closed_forms[-1].eval_at(acc).items()}

    thresholds = [-sp.oo] + thresholds[1:]
    conditions = []
    for i in range(len(thresholds) - 1):
        if thresholds[i] is -sp.oo and thresholds[i + 1] is sp.oo:
            conditions.append(z3.BoolVal(True))
        elif thresholds[i] is -sp.oo:
            conditions.append(rec.ind_var < thresholds[i + 1])
        elif thresholds[i + 1] is sp.oo:
            conditions.append(thresholds[i] <= rec.ind_var)
        else:
            conditions.append(z3.And(thresholds[i] <= rec.ind_var, rec.ind_var < thresholds[i + 1]))
    return PiecewiseClosedForm(rec.ind_var, conditions, closed_forms)

def _compute_candidate_solution(rec: Recurrence, start, n, ith, start_value, enable_bounded_cfinite=True):
    values, index_seq = rec.get_n_values_starts_with(start, n, start_value)
    compressed_seq = utils.compress_seq(index_seq)
    guessed_patterns = [seq for seq, _ in compressed_seq]
    debug_prefix = "%dth guess starts with %dth value: " % (ith, start)
    padding = " "*len(debug_prefix)
    logger.debug(debug_prefix + "values are %s" % values)
    logger.debug(padding + "prefix of index sequence is %s" % index_seq)
    logger.debug(padding + "the guessed patterns are %s" % guessed_patterns)
    first_value = values[0]
    initial = {func.decl()(0): first_value[func] for func in first_value}
    new_rec = rec.copy_rec_with_diff_initial(initial)
    sol = _compute_solution_by_index_seq(
        new_rec,
        compressed_seq,
        enable_bounded_cfinite=enable_bounded_cfinite,
    )
    # shift_sol = sol.subs({sol.ind_var: sol.ind_var - start})
    logger.debug(padding + "the guessed closed-form solution is %s" % sol)
    return sol, compressed_seq

def _solve_as_nonconditional(rec: Recurrence, seq, enable_bounded_cfinite=True):
    new_rec = LoopRecurrence.build_nonconditional_from_rec_by_seq(rec, seq, {})
    # new_rec.pprint()
    period = len(seq)
    try:
        solvable = is_solvable_map(new_rec)
    except Exception:
        solvable = False
    if solvable:
        raw_closed_form = solve_solvable_map(new_rec)
    elif enable_bounded_cfinite:
        try:
            if not can_attempt_bounded_cfinite_map(new_rec, require_nonlinear=True):
                raise BoundedCFiniteError("Bounded C-finite fallback is reserved for nonlinear maps.")
            raw_closed_form = solve_bounded_cfinite_map(new_rec).as_dict()
        except BoundedCFiniteError:
            raise UnsolvableError("Recurrence of this kind is not solvable.")
    else:
        raise UnsolvableError("Recurrence of this kind is not solvable.")
    closed_form = []
    # modulo part
    for i in range(period):
        # shift_closed = {v: c.subs({rec.ind_var: (rec.ind_var - i)/period}, simultaneous=True) for v, c in raw_closed_form.items()}
        mapping = [(period*rec.ind_var, rec.ind_var - i), (rec.ind_var*period, rec.ind_var - i), (rec.ind_var, (rec.ind_var - i)/period)]
        shift_closed = {v: z3.substitute(c, mapping) for v, c in raw_closed_form.items()}
        for j in seq[:i]:
            shift_closed = rec.run_one_iteration_for_ith_transition(shift_closed, j)
        closed_form.append(shift_closed)
    res = PeriodicClosedForm(closed_form, rec.ind_var)
    return res

def _set_up_constraints(rec: LoopRecurrence, closed_form: PiecewiseClosedForm, index_seq,
                        budget=None):
    # rec_conditions = [utils.to_z3(cond) for cond in rec.conditions]
    rec_conditions = rec.conditions
    # intervals = closed_form.intervals
    # k = z3.Int('__k0')
    # ks = [z3.Int('__k%d' % i) for i in range(len(index_seq))]
    ks = []
    constraint = True
    ind_var = closed_form.ind_var
    # closed_form.pprint()
    acc = z3.IntVal(0)
    k_cnt = 0

    for i, (seq, q) in enumerate(index_seq):
        closed_form_component = closed_form.closed_forms[i]
        if (i < len(index_seq)-1 and isinstance(q, int) and
                (budget is None or (budget.options.enabled and budget.remaining_ms >= 1))):
            start = time.monotonic()
            finite = []
            for repeat in range(q):
                for r, j in enumerate(seq):
                    mapping = list(closed_form_component.get_rth_part_closed_form(r).items())
                    condition = z3.substitute(rec_conditions[j], mapping)
                    finite.append(z3.simplify(z3.substitute(
                        condition, (ind_var, acc+repeat*len(seq)+r))))
            constraint = z3.And(constraint, *finite)
            if budget is not None:
                budget.stats['preprocessing_seconds'] += time.monotonic()-start
                budget.stats['finite_instantiations'] += q*len(seq)
            acc += len(seq)*q
            continue
        k = z3.Int('__k%d' % k_cnt, ctx=rec.ind_var.ctx)
        phase_conditions = []
        for r, j in enumerate(seq):
            mapping = list(closed_form_component.get_rth_part_closed_form(r).items())
            condition = z3.substitute(rec_conditions[j], mapping)
            # Quantify over complete cycles, not absolute indices constrained
            # by a congruence. Each residue is an explicit affine substitution.
            condition = z3.simplify(z3.substitute(condition, (ind_var, acc+len(seq)*k+r)))
            if i < len(index_seq)-1:
                condition = z3.Implies(k < q, condition)
            phase_conditions.append(z3.simplify(condition))
        phase_condition = z3.simplify(z3.And(*phase_conditions))
        if not z3.is_true(phase_condition):
            if k in utils.get_vars(phase_condition):
                ks.append(k)
                k_cnt += 1
            constraint = z3.And(constraint, phase_condition)
        acc += len(seq)*q
    return constraint, ks
