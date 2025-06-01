import logging
import sympy as sp
import z3
from .recurrence import MultiRecurrence, Recurrence, LoopRecurrence, BaseCase, RecursiveCase
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from . import utils
from functools import reduce
from .closed_form import MultiFuncClosedForm

logger = logging.getLogger(__name__)

class PivotCall:
    '''This class represents a pivot call for a recursive case where there are
    multiple recursive calls. Pivot call is one of those recursive calls s.t.
    all other calls have been computed during the computation of pivot call.'''
    def __init__(self, rec: MultiRecurrence, case: RecursiveCase, pivot_call_name, cached_computation):
        self.case = case
        self.pivot_call_name = pivot_call_name
        self.cached_computation = cached_computation
        self.rec = rec

    @property
    def num_cached_values(self):
        return self.case.num_rec_calls - 1

    def get_pivot_func(self):
        return self.case.recursive_calls[self.pivot_call_name]

    def __str__(self):
        return '(%s, %s)' % (self.pivot_call_name, self.cached_computation)

    def __repr__(self):
        return '(%s, %s)' % (self.pivot_call_name, self.cached_computation)

def rec2nearly_tail(rec: MultiRecurrence):
    assert(rec.number_ret() == 1)
    pivot_calls = get_pivot_calls(rec)
    # new_func = sp.Function('')
    func_sig = rec.func_sig
    # func_p = sp.Function(func_sig.name + '_p')
    func_decl = func_sig.decl()
    func_p = z3.Function(func_decl.name() + '_p', *[func_decl.domain(i) for i in range(func_decl.arity())], func_decl.range())
    base_cached, rec_cached = gen_cached_ops(pivot_calls)
    all_pivot_func = [call.get_pivot_func() for call in pivot_calls]
    ori_base_cases = rec.get_base_cases()
    ori_rec_cases = rec.get_rec_cases()
    new_func_sig = func_p(*func_sig.children())
    new_base_cases = ori_base_cases
    new_rec_cases = ori_rec_cases

    for i, cached in enumerate(base_cached):
        if isinstance(cached, list):
            new_args = [(new_base_cases[i].op + arg[0], arg[1]) for arg in cached]
            # new_base_cases[i].op = sp.Piecewise(*new_args)
            new_base_cases[i].op = new_args
        else:
            new_base_cases[i].op = new_base_cases[i].op + cached

    for i, cached in enumerate(rec_cached):
        # new_rec_cases[i].recursive_calls = {pivot_calls[i].pivot_call_name: all_pivot_func[i]}
        call = pivot_calls[i]
        # cached_names = [sp.Symbol('_a0', integer=True)] + get_ret_names(pivot_calls, i)
        cached_names = [z3.Int('_a0')] + get_ret_names(pivot_calls, i)
        case = pivot_calls[i].case
        ori_names = [call.pivot_call_name] + [name for name in case.recursive_calls.keys() if str(name) != str(call.pivot_call_name)]
        # mapping = {ori_name: cached_name for ori_name, cached_name in zip(ori_names, cached_names)}
        # op = tuple(o.subs(mapping, simultaneous=True) for o in new_rec_cases[i].op)
        mapping = [(ori_name, cached_name) for ori_name, cached_name in zip(ori_names, cached_names)]
        op = tuple(z3.substitute(o, mapping) for o in new_rec_cases[i].op)
        # mapped_cached = tuple(c.subs(mapping, simultaneous=True) for c in cached)
        mapped_cached = tuple(z3.substitute(c, mapping) for c in cached)
        new_rec_cases[i].op = op + mapped_cached

    cached_names = sum([get_ret_names(pivot_calls, i) for i in range(len(pivot_calls))], [])
    # names = [sp.Symbol('_a0', integer=True)] + cached_names
    names = [z3.Int('_a0')] + cached_names
    for i, pivot_call in enumerate(pivot_calls):
        old_recursive_calls = ori_rec_cases[i].recursive_calls
        new_func = func_p(*pivot_call.get_pivot_func().children())
        # The pivot call should be put as the first element
        # names = [pivot_call.pivot_call_name] + [name for name in old_recursive_calls if name != pivot_call.pivot_call_name]
        new_rec_cases[i].recursive_calls = {name: new_func for name in names}
    new_rec = MultiRecurrence(new_func_sig, new_base_cases, new_rec_cases)
    # new_rec.pprint()
    return new_rec

def gen_cached_ops(pivot_calls: list[PivotCall]):
    first_pivot_call = pivot_calls[0]
    # base_cached, rec_cached = first_pivot_call.gen_cached_op()
    base_cached, rec_cached = gen_cached_op(pivot_calls, 0)
    for i, pivot_call in enumerate(pivot_calls[1:], 1):
        # cur_base, cur_rec = pivot_call.gen_cached_op()
        cur_base, cur_rec = gen_cached_op(pivot_calls, i)
        for i, cached in enumerate(cur_base):
            base_cached[i] = base_cached[i] + cached
        for i, cached in enumerate(cur_rec):
            rec_cached[i] = rec_cached[i] + cached
    return base_cached, rec_cached

def get_ret_names(pivot_calls: list[PivotCall], i):
    num_prev_cached_values = sum(c.num_cached_values for c in pivot_calls[:i])
    call = pivot_calls[i]
    # plus one, because the first returned value is the original returned value
    # and is not counted as cached values
    # call_names = sp.symbols('_a%d:%d' % (num_prev_cached_values + 1, num_prev_cached_values + 1 + call.num_cached_values), integer=True)
    call_names = [z3.Int('_a%d' % i) for i in range(num_prev_cached_values + 1, num_prev_cached_values + 1 + call.num_cached_values)]
    return call_names

def gen_cached_op(pivot_calls: list[PivotCall], i):
        '''Generate vectorized recursive case for the ith pivot call'''
        call = pivot_calls[i]
        base_res = []
        rec_res = []
        ret_names = get_ret_names(pivot_calls, i)
        for cached_call_name in call.cached_computation:
            base_cached, rec_cached = call.cached_computation[cached_call_name]
            for i, cached in enumerate(base_cached):
                # if some path is infeasible, the value remains unchanged
                # op = cached if len(cached) != 0 else (sp.Integer(0),)
                op = cached # if len(cached) != 0 else tuple(ret_names)
                # op = (cached,)
                try:
                    if isinstance(op, list):
                        new_args = [(base_res[i] + arg[0], arg[1]) for arg in op]
                        base_res[i] = new_args
                    else:
                        base_res[i] = base_res[i] + op
                except IndexError:
                    base_res.append(op)
            for i, cached in enumerate(rec_cached):
                op = cached if len(cached) != 0 else tuple(ret_names)
                try:
                    rec_res[i] = rec_res[i] + op
                except IndexError:
                    rec_res.append(op)
        return base_res, rec_res

def possible_arity(rec: MultiRecurrence):
    arity = 1
    for case in rec.get_rec_cases():
        arity *= len(case.recursive_calls)
    return arity

def get_pivot_calls(rec: MultiRecurrence) -> list[PivotCall]:
    '''Pivot calls are those calls in recursive cases whose computations also
    compute other recursive calls in the same case. This function returns the
    list of tuple of form (pivot_call, cached_computation) in the same order of
    recursive cases stored in rec'''
    # rec_conditions, rec_calls, rec_ops = rec.get_rec_cases()
    rec_cases = rec.get_rec_cases()
    all_pivot_calls = []
    # for pre_cond, calls in zip(rec_conditions, rec_calls):
    # arity = possible_arity(rec)
    for rec_case in rec_cases:
        pre_cond = rec_case.condition
        calls = rec_case.recursive_calls
        if rec_case.is_tail():
            pivot_name = list(rec_case.recursive_calls.keys())[0]
            # names = [name for name in sp.symbols('_a:%d' % arity) if name != pivot_name]
            all_pivot_calls.append(PivotCall(rec, rec_case, list(rec_case.recursive_calls.keys())[0], {}))
            continue
        # if len(calls) <= 1:
        #     continue
        # name_terms = {}
        # for name, call in calls.items():
        #     equal_terms = eq_terms(call, pre_cond, rec)
        #     name_terms[name] = equal_terms
        #     name_terms[name].add(call)
        for pivot_call_name, pivot_call in calls.items():
            all_names = set(calls.keys())
            other_names = all_names - {pivot_call_name}
            is_all_computed = True
            cached_dict = {}
            for other_name in other_names:
                other_call = calls[other_name]
                # redundant_computation = [is_computed(pivot_call, other_call, pre_cond, rec) for other_call in name_terms[other_name]]
                redundant_computation = is_computed(pivot_call, other_call, pre_cond, rec)
                # cached_computations = [r for r in redundant_computation if len(r[0]) != 0 or len(r[1]) != 0]
                # if len(cached_computations) == 0:
                if len(redundant_computation[0]) == 0 and len(redundant_computation[1]) == 0:
                    is_all_computed = False
                    break
                # cached_computation = cached_computations[0]
                # cached_computation = redundant_computation
                cached_dict[other_name] = redundant_computation
            if is_all_computed:
                # all_pivot_calls.append((pivot_call_name, cached_dict))
                all_pivot_calls.append(PivotCall(rec, rec_case, pivot_call_name, cached_dict))
                break
        else:
            raise Exception('Not convertible')
    return all_pivot_calls

def inverse(mapping):
    '''inverse a linear transformation represented by mapping'''
    sp_mapping = {utils.to_sympy(k): utils.to_sympy(v) for k, v in mapping.items()}
    ys = sp.symbols('_y:%d' % len(sp_mapping), integer=True)
    # ys_mapping = {y: mapping[u] for y, u in zip(ys, mapping)}
    eqs = []
    # us = list(mapping.keys())
    us = list(sp_mapping.keys())
    for y, u in zip(ys, us):
        eqs.append(y - sp_mapping[u])
    sol_list = sp.solve(eqs, *us, dict=True)
    try:
        sol = sol_list[0]
    except:
        sol = sol_list
    ys_mapping = [(utils.to_z3(y), utils.to_z3(u)) for y, u in zip(ys, us)]
    # return {u: sol[u].subs(ys_mapping, simultaneous=True) for u in sol}
    z3_sol = {utils.to_z3(k): utils.to_z3(v) for k, v in sol.items()}
    # return {u: sol[u].subs(ys_mapping, simultaneous=True) for u in z3_sol}
    return {u: z3.substitute(z3_sol[u], ys_mapping) for u in z3_sol}

def is_computed(pivot_call, other_call, pre_cond, rec: MultiRecurrence):
    '''Check if in the computation of target_call, other_call is also computed'''
    unrolled_pivot_call = unroll(pivot_call, rec)
    unrolled_other_call = unroll(other_call, rec)
    base_pivot_conditions = [case.condition for case in unrolled_pivot_call.get_base_cases()]
    # base_other_conditions, base_other_post_ops = unrolled_other_call.get_base_cases()
    base_other_conditions = [case.condition for case in unrolled_other_call.get_base_cases()]
    base_other_post_ops = [case.op for case in unrolled_other_call.get_base_cases()]
    total_other_base_cond = reduce(z3.Or, base_other_conditions, z3.BoolVal(False))
    solver = z3.Solver()
    # if the pivot call goes into a base case, then other call should also goes into a base case
    if any([solver.check(pre_cond & base_target_cond & z3.Not(total_other_base_cond)) == z3.sat for base_target_cond in base_pivot_conditions]):
        return [], []
    base_res = []
    # for base_pivot_cond in base_pivot_conditions:
    for pivot_base_case in unrolled_pivot_call.get_base_cases():
        transformation = {u: pivot_call.arg(i) for i, u in enumerate(rec.func_sig.children())}
        inverse_mapping = list(inverse(transformation).items())
        computed = [z3.unsat == solver.check(pre_cond & pivot_base_case.condition & z3.Not(base_other_cond)) for base_other_cond in base_other_conditions]
        cur_op = [(base_other_post_ops[i], base_other_conditions[i]) for i, c in enumerate(computed) if c]
        cur_op[-1] = (cur_op[-1][0], z3.BoolVal(True))
        inversed_cur_op = [(tuple(z3.substitute(op, inverse_mapping) for op in cur_op_k), z3.substitute(cond, inverse_mapping)) for cur_op_k, cond in cur_op]
        # base_res.append(sp.Piecewise(*cur_op).subs(inverse_mapping, simultaneous=True))
        base_res.append(inversed_cur_op)
    # rec_pivot_conditions, rec_pivot_calls_list, _ = unrolled_pivot_call.get_rec_cases()
    rec_res = []
    # for rec_pivot_cond, rec_pivot_calls in zip(rec_pivot_conditions, rec_pivot_calls_list):
    for rec_case in unrolled_pivot_call.get_rec_cases():
        # if the pivot call goes into a recursive case (i), then
        # (1) if other call goes into a base case, it is okay;
        # (2) if other call goes in to a recursive case, then it should be equal
        # to some call in the recursive case (i)
        rec_pivot_cond = rec_case.condition
        rec_pivot_calls = rec_case.recursive_calls
        name_list = list(rec_pivot_calls.keys())
        if z3.unsat == solver.check(pre_cond & rec_pivot_cond):
            rec_res.append({})
            continue
        computed = [z3.unsat == solver.check(pre_cond & rec_pivot_cond & z3.Not(rec_pivot_calls[name] == other_call)) for name in name_list]
        if not any(computed):
            unrolled_base_cases, unrolled_rec_cases = unroll_by_pre_cond(other_call, pre_cond & rec_pivot_cond, rec)
            assert(len(unrolled_base_cases) + len(unrolled_rec_cases) <= 1)
            case = None
            if len(unrolled_base_cases) == 1:
                case = unrolled_base_cases[0]
                rec_res.append(case.op)
            elif len(unrolled_rec_cases) == 1:
                case = unrolled_rec_cases[0]
                assert(len(case.recursive_calls) == 1)
                eq_other_call = list(case.recursive_calls.values())[0]
                computed = [z3.unsat == solver.check(pre_cond & rec_pivot_cond & z3.Not(rec_pivot_calls[name] == eq_other_call)) for name in name_list]
            if case is None or not any(computed):
                return [], []
            # return [], []
        try:
            idx = computed.index(True)
            cur_res = (name_list[idx],)
            rec_res.append(cur_res)
        except ValueError:
            return [], []

    return base_res, rec_res

def unroll_by_pre_cond(call, pre_cond, rec):
    unrolled_call = unroll(call, rec)
    base_cases = unrolled_call.get_base_cases()
    rec_cases = unrolled_call.get_rec_cases()
    solver = z3.Solver()
    res_base_cases = []
    res_rec_cases = []
    for cases, res in zip([base_cases, rec_cases], [res_base_cases, res_rec_cases]):
        for case in cases:
            to_check = pre_cond & case.condition
            if z3.sat == solver.check(to_check):
                if isinstance(case, RecursiveCase) and len(case.recursive_calls) > 1:
                    continue
                res.append(case)
    return res_base_cases, res_rec_cases


def unroll(call, rec) -> MultiRecurrence:
    '''unroll the call according to the rec definition'''
    func_sig = rec.func_sig
    arity = len(func_sig.children())
    unroll_mapping = {func_sig.arg(i): call.arg(i) for i in range(arity)}
    unrolled_rec = rec.subs(unroll_mapping)
    return unrolled_rec

def solve_nearly_tail(rec: MultiRecurrence, is_array=False):
    assert(rec.is_nearly_tail())
    d = z3.Int('_d')
    D = z3.Int('_D')
    rets = z3.Ints(' '.join(['_ret%d' % i for i in range(rec.number_ret())]))
    loop_rec = nearly_tail2loop(rec, d, rets)
    loop_rec.pprint()
    loop_guard = get_loop_cond(rec, d)
    precondition = z3.BoolVal(True)
    if is_array:
        f = symbol2func(rec.func_sig.args[-1])
        loop_guard = z3.Or(loop_guard, z3.Eq(f(d), 0))
    loop_closed_form = solve_ultimately_periodic_symbolic(loop_rec, precondition=precondition)
    piecewise_D = compute_piecewise_D(d, D, loop_guard, loop_closed_form, precondition)
    scalar_closed_form = loop_closed_form.subs({d: piecewise_D})
    branches_rets = [[] for _ in range(len(rets))]
    for base_case in rec.get_base_cases():
        cond = base_case.condition
        ops = base_case.op
        if isinstance(ops, list) and len(ops) > 0 and isinstance(ops[0], tuple):
            assert(len(ops) == 1)
            ops = ops[0][0]
        vars_bases_case = reduce(set.union, [utils.get_vars(op) for op in ops])
        # args = utils.get_vars(cond) | reduce(set.union, [utils.get_vars(op) for op in ops])
        args = utils.get_vars(cond) | vars_bases_case
        symbol2func_mapping = {arg: symbol2func(arg)(d) for arg in args}
        op_funcs = (z3.substitute(op, *list(symbol2func_mapping.items())) for op in ops)
        cond_func = z3.substitute(cond, *list(symbol2func_mapping.items()))
        ops_D = (z3.substitute(op_func, *list(scalar_closed_form.as_dict().items())) for op_func in op_funcs)
        cond_D = z3.substitute(cond_func, *list(scalar_closed_form.as_dict().items()))
        for branches, op_D in zip(branches_rets, ops_D):
            branches.append((op_D, cond_D))
    for branches in branches_rets:
        branches[-1] = (branches[-1][0], True)
    # print("D = %s" % piecewise_D)
    # print(z3.simplify(z3.substitute(piecewise_D, (z3.Int('x'), z3.IntVal(4)))))
    # print(z3.simplify(z3.substitute(piecewise_D, (z3.Int('x'), z3.IntVal(5)))))
    # print(z3.simplify(z3.substitute(piecewise_D, (z3.Int('x'), z3.IntVal(6)))))
    # print(z3.simplify(z3.substitute(piecewise_D, (z3.Int('x'), z3.IntVal(7)))))
    rets0 = [utils.to_ite(branches) for branches in branches_rets]
    closed_form_dict = scalar_closed_form.as_dict()
    # tmp_closed = loop_closed_form.as_dict()
    # tmp_ret = z3.Function('_ret0', z3.IntSort(), z3.IntSort())
    # ret0 = z3.Int('_ret0')
    # print(z3.simplify(z3.substitute(piecewise_D, [(z3.Int('sarg'), z3.IntVal(1)), (z3.Int('sarg1'), z3.IntVal(-1))])))
    mapping = {ret: ret0 for ret, ret0 in zip(rets, rets0)} | {d: piecewise_D}
    return [z3.substitute(closed_form_dict[symbol2func(ret)(d)], *list(mapping.items())) for ret in rets]

def nearly_tail2loop(rec: MultiRecurrence, d, rets):
    assert(rec.is_nearly_tail())
    # recursive_calls = rec.recursive_calls
    # post_ops = rec.post_ops
    func_sig = rec.func_sig
    args = func_sig.children()
    # d = sp.Symbol('_d', integer=True)
    # ret = sp.Symbol('_ret', integer=True)
    args_func_map = {arg: symbol2func(arg) for arg in args} | {ret: symbol2func(ret) for ret in rets}
    # base_conditions, base_post_ops = rec.get_base_cases()
    # rec_conditions, recursive_calls, rec_post_ops = rec.get_rec_cases()
    args_func_d = {arg: symbol2func(arg)(d) for arg in args}
    args_func_d_1 = {arg: symbol2func(arg)(d + 1) for arg in args}
    rec_conditions = [case.condition for case in rec.get_rec_cases()]
    # loop_branch_conditions = [cond.subs(args_func_d, simultaneous=True) for cond in rec_conditions]
    loop_branch_conditions = [z3.substitute(cond, *list(args_func_d.items())) for cond in rec_conditions]
    transitions = []
    # for i, rec_call in enumerate(recursive_calls):
    ret_funcs = [symbol2func(ret) for ret in rets]
    for rec_case in rec.get_rec_cases():
        rec_call = rec_case.recursive_calls
        args_p = list(rec_call.values())[0].children()
        post_ops = rec_case.op
        transition = {z3.simplify(args_func_d_1[arg]): z3.substitute(arg_p, *list(args_func_d.items())) for arg, arg_p in zip(args, args_p)}
        names = list(rec_call.keys())
        mapping = {name: ret_func(d) for name, ret_func in zip(names, ret_funcs)} | args_func_d
        ret_trans = {z3.simplify(ret_func(d + 1)): z3.substitute(post_op, list(mapping.items())) for ret_func, post_op in zip(ret_funcs, post_ops)}
        transition |= ret_trans
        transitions.append(transition)
    initial = {args_func_map[ret](0): ret for ret in rets} | {symbol2func(arg)(0): arg for arg in args}
    loop_rec = LoopRecurrence.mk_rec(initial, loop_branch_conditions, transitions)
    loop_rec.set_reverse({z3.simplify(ret_func(d + 1)) for ret_func in ret_funcs})
    return loop_rec

# def _transform_ret(ret_dict):
#     dim = len(ret_dict) + 1
#     m_coeffs = [sp.Symbol('_ret_m%d_%d' % (i, j)) for j in range(dim) for i in range(dim)]
#     m = sp.Matrix(m_coeffs).reshape(dim, dim)
#     print(m)
#     print('hhh')


def get_loop_cond(rec: MultiRecurrence, d):
    assert(rec.is_nearly_tail())
    # base_conditions, _ = rec.get_base_cases()
    base_conditions = [case.condition for case in rec.get_base_cases()]
    func_sig = rec.func_sig
    args = func_sig.children()
    args_func_d = {arg: symbol2func(arg)(d) for arg in args}
    disjunct_base_condition = reduce(z3.Or, base_conditions, z3.BoolVal(False))
    # functional_base_condition = disjunct_base_condition.subs(args_func_d, simultaneous=True)
    functional_base_condition = z3.substitute(disjunct_base_condition, *list(args_func_d.items()))
    return functional_base_condition

def compute_piecewise_D(d, D, loop_cond, loop_closed_form, precondition):
    '''Assume D is piecewise linear'''
    d_z3 = d
    D_z3 = D
    branches = []
    hints = []
    for i, (cur_case, closed) in enumerate(zip(loop_closed_form._constraints, loop_closed_form._closed_forms)):
        logger.debug('Handling case %d/%d' % (i + 1, len(loop_closed_form._constraints)))
        terminate_cond = z3.simplify(z3.substitute(loop_cond, *list(closed.as_dict().items())), elim_ite=True)
        cur_D = compute_D_by_case(d_z3, D_z3, z3.Not(terminate_cond), cur_case, hints)
        if cur_D is not None:
            hints.append(cur_D)
            branches.append((cur_D, cur_case))
    branches[-1] = (branches[-1][0], True)
    return z3.simplify(utils.to_ite(branches))

def compute_D_by_case(d, D, loop_cond, case, hints):
    '''Assume D is linear under the case'''
    cond = loop_cond
    case_z3 = case
    axioms = set()
    # smallest D that violates the loop condition
    axioms.add(case_z3)
    axioms.add(z3.ForAll([d], z3.Implies(z3.And(0 <= d, d < D), cond)))
    axioms.add(z3.substitute(z3.Not(cond), (d, D)))
    axioms.add(D >= 0)

    qe = z3.Then(z3.Tactic('qe'), z3.Tactic('ctx-solver-simplify'), z3.Tactic('simplify'))
    # constraints = z3.simplify(z3.Or(*[z3.And(*c) for c in qe(z3.And(*axioms))]))
    constraints = qe(z3.And(*axioms)).as_expr()
    sol = utils.solve_piecewise_sol(constraints, [D], z3.Int)
    try:
        return sol.to_z3()[D]
    except:
        return z3.IntVal(0)

def symbol2func(sym):
    return z3.Function(sym.decl().name(), z3.IntSort(), z3.IntSort())

def solve_multivariate_rec(rec: MultiRecurrence):
    rec.pprint()
    if rec.is_nearly_tail():
        closed_forms = solve_nearly_tail(rec)
    else:
        new_rec = rec2nearly_tail(rec)
        # new_rec.pprint()
        closed_forms = solve_nearly_tail(new_rec)
        # raise Exception('not a nearly tail recursion')
    return MultiFuncClosedForm(rec.func_sig, closed_forms[0])
