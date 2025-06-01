import sympy as sp
from sympy.core.function import AppliedUndef
import logging
import z3
from functools import reduce
from itertools import product

from . import utils
from .recurrence import Recurrence, LoopRecurrence
from .closed_form import PeriodicClosedForm, PiecewiseClosedForm, SymbolicClosedForm
from .solvable_polynomial import solve_solvable_map, is_solvable_map
from .logic_simplification import DNFConverter

logger = logging.getLogger(__name__)

class UnsolvableError(Exception):
    pass

def solve_ultimately_periodic_symbolic(rec: LoopRecurrence, bnd=100, precondition=z3.BoolVal(True)):
    z3_solver = z3.Solver()
    z3_solver.add(precondition)
    acc_condition = z3.BoolVal(True)
    i = 0
    logger.debug("Solving recurrence %s" % rec)
    # rec.pprint()
    constraints = []
    closed_forms = []
    if rec.is_all_initialized():
        return solve_ultimately_periodic_initial(rec, bnd)
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
        _, index_seq = _solve_ultimately_periodic_initial(initialized_rec)
        qs = [z3.Int('q%d' % i) for i in range(len(index_seq) - 1)]
        # qs = sp.symbols('q:%d' % (len(index_seq) - 1), integer=True)
        index_seq_temp = [(s[0], q) for s, q in zip(index_seq, qs)] + [index_seq[-1]]
        can_sol = _compute_solution_by_index_seq(rec, index_seq_temp)
        quantified_constraint, ks = _set_up_constraints(rec, can_sol, index_seq_temp)
        quantified_constraint = z3.simplify(z3.And(quantified_constraint, *[q >= 1 for q in qs]))
        # qe = z3.Then('qe', 'ctx-solver-simplify')
        qe = z3.Then('simplify', 'qe', 'simplify')
        # constraint = z3.And(*qe.apply(z3.ForAll(k, z3.Implies(k >= 0, quantified_constraint)))[0])
        full_constraint = z3.ForAll(ks, z3.Implies(z3.And([k >= 0 for k in ks]), quantified_constraint))
        constraint = z3.simplify(qe.apply(full_constraint).as_expr())
        logger.debug('In the %dth iteration: the sampled initial values are %s' % (i, cur_val))
        logger.debug('In the %dth iteration: the index sequence is %s' % (i, index_seq))
        logger.debug('In the %dth iteration: the set up index sequence template is %s' % (i, index_seq_temp))
        logger.debug('In the %dth iteration: the closed-form solution is\n%s' % (i, can_sol))
        logger.debug('In the %dth iteration: the q\'s are %s' % (i, qs))
        q_linear = utils.solve_piecewise_sol(constraint, qs, sort=z3.Int)
        # print(constraint)
        logger.debug('In the %dth iteration: the q\'s solutions are %s' % (i, q_linear.to_piecewise()))
        for q_constraint, q_sol in zip(q_linear.conditions, q_linear.expressions):
            constraint_no_q = z3.substitute(z3.And(constraint, q_constraint), *[(q, q_sol[q]) for q in qs])
            qe = z3.Tactic('qe')
            forall_constraint = z3.ForAll(ks, z3.Implies(z3.And([k >= 0 for k in ks]), constraint_no_q))
            constraint_no_kq = z3.simplify(z3.Or(*[z3.And(*conjunct) for conjunct in qe.apply(forall_constraint)]))
            constraints.append(constraint_no_kq)
            acc_condition = z3.And(acc_condition, z3.Not(constraint_no_kq))
            closed_forms.append(can_sol.simple_subs(q_sol))
            # closed_forms[-1].pprint()
        # exit(0)
    res = SymbolicClosedForm(constraints, closed_forms, rec.ind_var)
    return res
    

# def _unpack_q_expressions(q_linear):
#     conditions = q_linear.conditions
#     expressions = q_linear.expressions
#     conditions = [conditional_e.conditions for conditional_e in q_linear]
#     expressions = [conditional_e.expressions for conditional_e in q_linear]
#     lengths = [len(conditional_e.conditions) for conditional_e in q_linear]
#     indices = [tuple(range(l)) for l in lengths]
#     for index in product(*indices):
#         yield z3.And(*[condition[i] for i, condition in zip(index, conditions)]), [expression[i] for i, expression in zip(index, expressions)]

def solve_ultimately_periodic_initial(rec: LoopRecurrence, bnd=100):
    closed_form, _ = _solve_ultimately_periodic_initial(rec, bnd)
    return closed_form

def _solve_ultimately_periodic_initial(rec: LoopRecurrence, bnd=100):
    assert(rec.is_all_initialized())
    n = 10
    start = 0
    ith = 1
    closed_form = PiecewiseClosedForm(rec.ind_var)
    acc_index_seq = []
    start_value = rec.initial
    while n < bnd:
        n *= 2
        candidate, guessed_index_seq = _compute_candidate_solution(rec, start, n, ith, start_value=start_value)
        acc_index_seq.extend(guessed_index_seq)
        smallest = verify(rec, candidate, guessed_index_seq)
        shift_candidate = candidate.subs({candidate.ind_var: candidate.ind_var - start})
        closed_form = closed_form.concatenate(shift_candidate, start)
        # if smallest is not sp.oo: # means hypothesis is wrong
        if smallest is not None:
            start = smallest
        else:
            break
        start_value = closed_form.eval_at(start).as_dict()
        start_value = {z3.substitute(k, (rec.ind_var, z3.IntVal(start))): v for k, v in start_value.items()}
        ith += 1
    res = closed_form, utils.compress_seq(utils.flatten_seq(acc_index_seq))
    return res

def verify(rec: Recurrence, candidate_sol: PiecewiseClosedForm, pattern: list):
    conditions = rec.conditions
    start = sum([cnt for _, cnt in pattern[:-1]])
    last_closed_form = candidate_sol.closed_forms[-1]
    assert(isinstance(last_closed_form, PeriodicClosedForm))
    period = last_closed_form.period
    periodic_index_seq, _ = pattern[-1]
    n = z3.Int('__n')
    k = z3.Int('__k')
    n_range = n >= start
    candidate_sol.pprint()
    smallest = None
    for r, i in enumerate(periodic_index_seq):
        cond = conditions[i]
        if cond == z3.BoolVal(True):
            continue
        # cond = cond.subs(last_closed_form.get_rth_part_closed_form(r), simultaneous=True)
        # cond = cond.subs({candidate_sol.ind_var: period*k + r}, simultaneous=True)
        mapping = [(k, v if z3.is_int(v) else z3.ToInt(v)) for k, v in last_closed_form.get_rth_part_closed_form(r).items()]
        # print(list(last_closed_form.get_rth_part_closed_form(r).items()))
        # cond = z3.substitute(cond, *list(last_closed_form.get_rth_part_closed_form(r).items()))
        cond = z3.substitute(cond, mapping)
        cond = z3.substitute(cond, (candidate_sol.ind_var, period*k + r))
        # k_range = n_range.subs({n: period*k + r}, simultaneous=True).as_set()
        k_range = z3.substitute(n_range, (n, period*k + r))
        solver = z3.Solver()
        # if not k_range.is_subset(cond_range):
        solver.add(k_range)
        solver.add(z3.Not(cond))
        if solver.check() == z3.sat:
            cur_smallest = _smallest_violation(k_range, cond, k)
            cur_smallest = z3.simplify(cur_smallest*period + r).as_long()
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

def _compute_solution_by_index_seq(rec: LoopRecurrence, index_seq):
    patterns = [seq for seq, _ in index_seq]
    acc_thresholds = [sum(len(seq)*cnt for seq, cnt in index_seq[:i]) for i in range(1, len(index_seq))]
    # nums = [cnt for _, cnt in index_seq]
    thresholds = [0] + acc_thresholds + [sp.oo]
    nonconditional = [_solve_as_nonconditional(rec, pattern) for pattern in patterns]
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

def _compute_candidate_solution(rec: Recurrence, start, n, ith, start_value):
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
    sol = _compute_solution_by_index_seq(new_rec, compressed_seq)
    # shift_sol = sol.subs({sol.ind_var: sol.ind_var - start})
    logger.debug(padding + "the guessed closed-form solution is %s" % sol)
    return sol, compressed_seq

def _solve_as_nonconditional(rec: Recurrence, seq):
    new_rec = LoopRecurrence.build_nonconditional_from_rec_by_seq(rec, seq, {})
    # new_rec.pprint()
    period = len(seq)
    if is_solvable_map(new_rec):
        closed_form = []
        raw_closed_form = solve_solvable_map(new_rec)
        # modulo part
        for i in range(period):
            # shift_closed = {v: c.subs({rec.ind_var: (rec.ind_var - i)/period}, simultaneous=True) for v, c in raw_closed_form.items()}
            shift_closed = {v: z3.substitute(c, (rec.ind_var, (rec.ind_var - i)/period)) for v, c in raw_closed_form.items()}
            for j in seq[:i]:
                shift_closed = rec.run_one_iteration_for_ith_transition(shift_closed, j)
            closed_form.append(shift_closed)
        res = PeriodicClosedForm(closed_form, rec.ind_var)
    else:
        raise UnsolvableError("Recurrence of this kind is not solvable.")
    return res

def _set_up_constraints(rec: LoopRecurrence, closed_form: PiecewiseClosedForm, index_seq):
    # rec_conditions = [utils.to_z3(cond) for cond in rec.conditions]
    rec_conditions = rec.conditions
    # intervals = closed_form.intervals
    # k = z3.Int('__k0')
    # ks = [z3.Int('__k%d' % i) for i in range(len(index_seq))]
    ks = []
    constraint = True
    ind_var = closed_form.ind_var
    closed_form.pprint()
    acc = z3.IntVal(0)
    k_cnt = 0
    for i, (seq, q) in enumerate(index_seq):
        piece_premise = closed_form.conditions[i]
        closed_form_component = closed_form.closed_forms[i]
        # period = closed_form_component.period
        solver = z3.Solver()
        for r, j in enumerate(seq):
            k = z3.Int('__k%d' % k_cnt)
            ks.append(k)
            k_cnt += 1
            if solver.check(z3.Not(rec.conditions[j])) == z3.unsat:
                continue
            premise = z3.simplify(z3.And(piece_premise, (k - acc) % len(seq) == r))
            mentioned_vars = utils.get_applied_functions(rec.conditions[j])
            closed_form_z3 = closed_form_component.selected_to_z3(mentioned_vars)
            mapping = [(kk, v) for kk, v in closed_form_z3.items()]
            condition = z3.substitute(rec_conditions[j], *mapping)
            cur_constraint = z3.Implies(premise, condition)
            cur_constraint = z3.substitute(cur_constraint, (ind_var, k))
            constraint = z3.And(constraint, cur_constraint)
        acc += acc + q
    return constraint, ks
