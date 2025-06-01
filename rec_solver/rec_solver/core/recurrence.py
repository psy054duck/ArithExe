import sympy as sp
# from sympy.core.function import AppliedUndef
from . import utils
import z3
from functools import reduce
from collections import defaultdict
from .logic_simplification import DNFConverter, to_cnf

class Recurrence:
    def __new__(cls, branches):
        base_conditions, base_transitions, recursive_conditions, recursive_transitions = Recurrence.divide_to_base_and_recursive(branches)
        if Recurrence.is_loop_recurrence(branches):
            ind_var = Recurrence.get_possible_ind_var(recursive_transitions)
            random_rec_case = recursive_transitions[0]
            # initial = {f.subs({ind_var: -1}, simultaneous=True): sp.Symbol(f.name, integer=True) for f in random_rec_case}
            initial = {z3.substitute(f, (ind_var, z3.IntVal(-1))): z3.Int(f.decl().name()) for f in random_rec_case}
            new_branches = list(zip(recursive_conditions, recursive_transitions))
            return LoopRecurrence(initial, new_branches)
        else:
            random_rec_case = recursive_transitions[0]
            func_sig = list(random_rec_case.keys())[0]
            return MultiRecurrence(branches)

    @staticmethod
    def mk_loop_recurrence(initial, branches):
        assert(Recurrence.is_loop_recurrence(branches))
        _, _, recursive_conditions, recursive_transitions = Recurrence.divide_to_base_and_recursive(branches)
        ind_var = Recurrence.get_possible_ind_var(recursive_transitions)
        random_rec_case = recursive_transitions[0]
        # initial = {f.subs({ind_var: -1}, simultaneous=True): sp.Symbol(f.name, integer=True) for f in random_rec_case} | initial
        initial = {z3.substitute(f, (ind_var, z3.IntVal(-1))): z3.Int(f.decl().name()) for f in random_rec_case} | initial
        new_branches = list(zip(recursive_conditions, recursive_transitions))
        return LoopRecurrence(initial, new_branches)


    @staticmethod
    def divide_to_base_and_recursive(branches):
        base_conditions = []
        recursive_conditions = []
        base_transitions = []
        recursive_transitions = []
        for cond, trans in branches:
            if Recurrence.is_base_condition(cond) and Recurrence.is_base_transition(trans):
                base_conditions.append(cond)
                base_transitions.append(trans)
            else:
                recursive_conditions.append(cond)
                recursive_transitions.append(trans)
        return base_conditions, base_transitions, recursive_conditions, recursive_transitions

    @staticmethod
    def is_base_condition(cond):
        # return len(cond.atoms(AppliedUndef)) == 0
        return not utils.contains_function_symbols(cond)

    @staticmethod
    def is_base_transition(trans):
        # return all([len(e.atoms(AppliedUndef)) == 0 for e in trans.values()])
        return all([not utils.contains_function_symbols(e) for e in trans.values()])

    @staticmethod
    def is_loop_recurrence(branches):
        conditions = [branch[0] for branch in branches]
        transitions = [branch[1] for branch in branches]
        for cond, trans in zip(conditions, transitions):
            # applied_func = cond.atoms(AppliedUndef)
            # applied_func |= reduce(set.union, [e.atoms(AppliedUndef) for e in trans.keys()])
            # applied_func |= reduce(set.union, [e.atoms(AppliedUndef) for e in trans.values()])
            applied_func = utils.get_applied_functions(cond)
            applied_func |= reduce(set.union, [utils.get_applied_functions(e) for e in trans.keys()])
            applied_func |= reduce(set.union, [utils.get_applied_functions(e) for e in trans.values()])
            # check if all functions are univariate
            # if not all([list(f.nargs)[0] == 1 for f in applied_func]):
            #     return False
            if not all([len(f.children()) == 1 for f in applied_func]):
                return False
            # check if the inductive variable is the only symbol in arguments
            ind_var = Recurrence.get_possible_ind_var(transitions)
            args = reduce(set.union, [set(f.children()) for f in applied_func])
            if not all([len(utils.get_vars(arg)) == 1 for arg in args]) and not all([list(arg.free_symbols)[0] == ind_var for arg in args]):
                return False
            # check if all recursive cases are of form f(n + 1) = g(f(n))
            solver = z3.Solver()
            if not all([solver.check(arg != ind_var) == z3.unsat or solver.check(arg != ind_var + 1) for arg in args]):
                return False
            for k in trans:
                if solver.check(k.arg(0) == ind_var + 1) == z3.unsat:
                    return False
        return True

    @staticmethod
    def make_exclusive_conditions(conditions):
        acc_neg = z3.BoolVal(True)
        exclusive_conditions = []
        for cond in conditions:
            exclusive_conditions.append(z3.simplify(z3.And(acc_neg, cond)))
            acc_neg = z3.And(acc_neg, z3.Not(cond))
        solver = z3.Solver()
        if solver.check(acc_neg) == z3.sat:
            exclusive_conditions.append(acc_neg)
        # simplified = [utils.formula2dnf(cond) for cond in exclusive_conditions]
        # simplifier = DNFConverter()
        # simplified = [z3.Or([z3.And(c) for c in simplifier.to_dnf(cond)]) for cond in exclusive_conditions]
        # simplified = [z3.simplify(c) for c in simplified]
        simplified = [z3.And(list(to_cnf(cond))) for cond in exclusive_conditions]
        simplified = [z3.simplify(c) for c in simplified]
        return simplified

    @staticmethod
    def get_initial(conditions, transitions, ind_var):
        '''get the initial values if the recurrence is from a loop'''
        assert(Recurrence.is_loop_recurrence(conditions, transitions))
        for cond, trans in zip(conditions, transitions):
            if Recurrence.is_base_condition(cond) and Recurrence.is_base_transition(trans):
                # assume cond is of form n == 0, where 'n' is the loop counter and c is a constant
                initial = {f.subs({ind_var: 0}): trans[f] for f in trans}
                break
        else:
            raise Exception('there is no base case')
        return initial

    @staticmethod
    def get_possible_ind_var(transitions):
        '''treat the only symbol in the first argument as the inductive variable'''
        first_trans = transitions[0]
        first_applied_func = list(first_trans.keys())[0]
        first_arg = first_applied_func.arg(0)
        # return list(first_arg.free_symbols)[0]
        return list(utils.get_vars(first_arg))[0]

class BaseCase:
    def __init__(self, condition, op):
        self.condition = condition
        self.op = op

    def subs(self, mapping):
        # condition = self.condition.subs(mapping, simultaneous=True)
        condition = z3.substitute(self.condition, list(mapping.items()))
        # ops = tuple(op.subs(mapping, simultaneous=True) for op in self.op)
        ops = tuple(z3.substitute(op, list(mapping.items())) for op in self.op)
        return BaseCase(condition, ops)

    def __str__(self):
        try:
            [op] = self.op
        except ValueError:
            op = self.op
        return '{}if {}'.format(op, self.condition)

class RecursiveCase:
    def __init__(self, condition, recursive_calls, op):
        self.condition = condition
        self.recursive_calls = recursive_calls
        self.op = op

    def subs(self, mapping):
        # condition = self.condition.subs(mapping, simultaneous=True)
        condition = z3.substitute(self.condition, list(mapping.items()))
        # recursive_calls = {name: self.recursive_calls[name].subs(mapping, simultaneous=True) for name in self.recursive_calls}
        recursive_calls = {name: z3.substitute(self.recursive_calls[name], list(mapping.items())) for name in self.recursive_calls}
        # ops = tuple(op.subs(mapping, simultaneous=True) for op in self.op)
        ops = tuple(z3.substitute(op, list(mapping.items())) for op in self.op)
        return RecursiveCase(condition, recursive_calls, ops)

    def is_tail(self):
        return len(set(self.recursive_calls)) <= 1

    @property
    def num_rec_calls(self):
        return len(self.recursive_calls)

    def __str__(self):
        ops = (op.subs(self.recursive_calls) for op in self.op)
        try:
            [op] = ops
        except ValueError:
            op = ops
        '{<50}if {}'.format(self.op, self.condition)

class MultiRecurrence:
    def __init__(self, *args):
        if isinstance(args[0], list):
            branches = args[0]
            conditions = [branch[0] for branch in branches]
            self._conditions = Recurrence.make_exclusive_conditions(conditions)
            transitions = [branch[1] for i, branch in enumerate(branches) if not z3.is_true(self._conditions[i])]
            self._conditions = [cond for cond in self._conditions if not z3.is_true(cond)]
            self.func_sig = list(transitions[0].keys())[0]
            self._recursive_calls, self._post_ops = self._preprocess_transitions(transitions)
        elif len(args) == 3:
            self.func_sig = args[0]
            self._base_cases = args[1]
            self._recursive_cases = args[2]
            return
        else:
            self.func_sig, self._conditions, self._transitions, self._recursive_calls, self._post_ops = args
        base_conditions, _, ops = self._get_cases(True)
        self._base_cases = [BaseCase(cond, op) for cond, op in zip(base_conditions, ops)]
        rec_conditions, recursive_calls, ops = self._get_cases(False)
        self._recursive_cases = [RecursiveCase(cond, call, op) for cond, call, op in zip(rec_conditions, recursive_calls, ops)]

    def number_ret(self):
        any_case = self.get_base_cases()[0]
        if isinstance(any_case.op, list):
            arg = any_case.op[0]
            return len(arg[0])
        return len(any_case.op)

    def subs(self, mapping):
        # func_sig = self.func_sig.subs(mapping, simultaneous=True)
        func_sig = z3.substitute(self.func_sig, list(mapping.items()))
        # conditions  = [cond.subs(mapping, simultaneous=True) for cond in self.conditions]
        # transitions = []
        # for transition in self.transitions:
        #     transitions.append({k.subs(mapping, simultaneous=True): tuple(op.subs(mapping, simultaneous=True) for op in t) for k, t in transition.items()})
        # recursive_calls = []
        # for call in self.recursive_calls:
        #     recursive_calls.append({k: call[k].subs(mapping, simultaneous=True) for k in call})
        # return MultiRecurrence(func_sig, conditions, transitions, recursive_calls, self.post_ops)
        base_cases = []
        recursive_cases = []
        for case in self.get_base_cases():
            base_cases.append(case.subs(mapping))
        for case in self.get_rec_cases():
            recursive_cases.append(case.subs(mapping))
        return MultiRecurrence(func_sig, base_cases, recursive_cases)

    @property
    def transitions(self):
        transitions = []
        for case in self.base_cases:
            transitions.append({self.func_sig: case.op})
        for case in self.recursive_cases:
            op = tuple(op.subs(case.recursive_calls, simultaneous=True) for op in case.op)
            transitions.append({self.func_sig: op})
        return transitions

    @property
    def conditions(self):
        # return self._conditions.copy()
        conditions = [case.condition for case in self.base_cases + self.recursive_cases]
        return conditions

    # @property
    # def transitions(self):
    #     return self._transitions.copy()

    def _preprocess_transitions(self, transitions):
        recursive_calls = []
        post_ops = []
        for trans in transitions:
            assert(len(trans.keys()) == 1)
            rhs = list(trans.values())[0]
            func_apps = [app for app in utils.get_applied_functions(rhs) if app.decl() == self.func_sig.decl()]
            # func_apps = list(rhs.atoms(self.func_sig))
            # named_calls = {sp.Symbol('_a%d' % i, integer=True): func_apps[i] for i in range(len(func_apps))}
            named_calls = {z3.Int('_a%d' % i): func_apps[i] for i in range(len(func_apps))}
            # call_names = {func_apps[i]: z3.Int('_a%d' % i) for i in range(len(func_apps))}
            # post_op = rhs.subs(call_names, simultaneous=True)
            call_names = [(func_apps[i], z3.Int('_a%d' % i)) for i in range(len(func_apps))]
            post_op = z3.substitute(rhs, call_names)
            recursive_calls.append(named_calls)
            post_ops.append((post_op,))
        return recursive_calls, post_ops

    def sympify(self):
        # exp = defaultdict(list)
        pieces = []
        for case in self.get_base_cases():
            ops = [utils.to_sympy(op) for op in case.op]
            cond = utils.to_sympy(case.condition)
            try:
                [op] = ops
            except ValueError:
                op = ops
            pieces.append((op, cond))
        for i, case in enumerate(self.get_rec_cases()):
            cond = utils.to_sympy(case.condition)
            # rec_calls = case.recursive_calls
            # ops = tuple(op.subs(rec_calls, simultaneous=True) for op in case.op)
            this_recursive_calls = self._recursive_calls[len(self.get_base_cases()) + i]
            # ops = [utils.to_sympy(this_recursive_calls[op]) for op in case.op]
            ops = [utils.to_sympy(z3.substitute(op, *list(this_recursive_calls.items()))) for op in case.op]
            try:
                [op] = ops
            except ValueError:
                op = ops
            pieces.append((op, cond))
        pieces[-1] = (pieces[-1][0], True)
        return sp.Piecewise(*pieces)

    def pprint(self):
        sp.init_printing()
        rec_sym = self.sympify()
        sp.pprint(sp.Eq(utils.to_sympy(self.func_sig), rec_sym))
        print()
        # for f in rec_sym:
        #     sp.pprint(sp.Eq(f, rec_sym[f]))
        #     print()

    def is_nearly_tail(self):
        # return all([len(set(calls.values())) <= 1 for calls in self.recursive_calls])
        return all([len(set(case.recursive_calls.values())) <= 1 for case in self.get_rec_cases()])

    def get_base_cases(self):
        return self._base_cases.copy()
        # conditions, _, post_ops = self._get_cases()
        # return conditions, post_ops

    def get_rec_cases(self):
        # return self._get_cases(False)
        return self._recursive_cases.copy()

    def _get_cases(self, is_base=True):
        '''Should be invoked only in __init__'''
        p = lambda calls: len(calls) == 0
        if not is_base:
             p = lambda calls: len(calls) > 0
        conditions = []
        recursive_calls = []
        post_ops = []
        for i, rec_calls in enumerate(self._recursive_calls):
            if p(rec_calls):
                conditions.append(self._conditions[i])
                recursive_calls.append(self._recursive_calls[i])
                post_ops.append(self._post_ops[i])
        return conditions, recursive_calls, post_ops

    def is_base_condition(self, cond):
        func_apps = cond.atoms(self.func_sig)
        return len(func_apps) == 0

    def is_base_transition(self, trans):
        for f in trans:
            func_apps = trans[f].atoms(self.func_sig)
            if len(func_apps) > 0:
                return False
        return True

class LoopRecurrence:
    def __init__(self, initial, branches):
        self._preprocess(initial, branches)
        self.reverses = set()

    def _preprocess(self, initial, branches):
        conditions = [branch[0] for branch in branches]
        self._conditions = Recurrence.make_exclusive_conditions(conditions)
        self._transitions = [branch[1] for branch in branches]
        if len(self._conditions) > len(conditions):
            self._transitions = [branch[1] for branch in branches] + [{}]
        self._initial = initial
        app = self.get_app()
        # print(app)
        last_args = {a.children()[-1] for a in app if a.decl().kind() == z3.Z3_OP_UNINTERPRETED}
        if len(last_args) > 1:
            raise Exception("More than one induction variable")
        self._ind_var = last_args.pop()
        # print(self._ind_var)
        self._transitions = self._padding_transitions(self._transitions, self.all_funcs)
        self._func_decls = self._get_all_func_decl()
        zero_indexed_func_apps = [func_decl(0) for func_decl in self.func_decls if func_decl.arity() == 1]
        self._initial = {zero_app: initial.get(zero_app, zero_app) for zero_app in zero_indexed_func_apps}
        self._closed_forms = {}

    # def __init__(self, branches):
    #     self._base_conditions = []
    #     self._recursive_conditions = []
    #     self._base_transitions = []
    #     self._recursive_transitions = []
    #     for cond, trans in branches:
    #         if self.is_base_case(cond, trans):
    #             self._base_conditions.append(cond)
    #             self._base_transitions.append(trans)
    #         else:
    #             self._recursive_conditions.append(cond)
    #             self._recursive_transitions.append(trans)
    def set_reverse(self, reverse_set):
        '''This functions is currently designed for multivariate recurrence solving.
           reverse_set is a subset of keys in transitions.
           Normally, in an iteration, the corresponding transition should be left composited.
           But if some keys are in reverse_set, the transition should be right composited.'''
        self.reverses = reverse_set
        
    @staticmethod
    def mk_rec(initial, conditions, transitions):
        branches = list(zip(conditions, transitions))
        return LoopRecurrence(initial, branches)

    def _padding_transitions(self, transitions, all_funcs):
        new_transitions = []
        # all_funcs_next = [func.subs({self.ind_var: self.ind_var + 1}) for func in all_funcs]
        all_funcs_next = [z3.simplify(z3.substitute(func, (self.ind_var, self.ind_var + 1))) for func in all_funcs]
        for trans in transitions:
            need_padding = set(all_funcs_next) - {z3.simplify(k) for k in trans.keys()}
            # identity_trans = {k: k.subs({self.ind_var: self.ind_var - 1}) for k in need_padding}
            identity_trans = {z3.simplify(k): z3.simplify(z3.substitute(k, (self.ind_var, self.ind_var - 1))) for k in need_padding}
            new_transitions.append(trans | identity_trans)
        return new_transitions

    def simplify_with_partial_solution(self, closed_forms):
        # self._conditions = [branch.subs(closed_forms) for branch in self._conditions]
        # self._transitions = [{k: v.subs(closed_forms) for k, v in trans.items()} for trans in self._transitions]
        mapping = list(closed_forms.items())
        mapping = [(z3.simplify(k), z3.simplify(v) if z3.is_int(v) else z3.ToInt(v)) for k, v in mapping]
        self._conditions = [z3.substitute(branch, *mapping) for branch in self._conditions]
        self._transitions = [{k: z3.substitute(v, *mapping) for k, v in trans.items()} for trans in self._transitions]
        self._closed_forms |= closed_forms

    def subs(self, mapping):
        # initial = {k: self.initial[k].subs(mapping, simultaneous=True) for k in self.initial}
        list_mapping = list(mapping.items())
        initial = {k: z3.substitute(self.initial[k], *list_mapping) for k in self.initial}
        # conditions = [c.subs(mapping, simultaneous=True) for c in self.conditions]
        conditions = [z3.substitute(c, *list_mapping) for c in self.conditions]
        # transitions = [{k: trans[k].subs(mapping, simultaneous=True) for k in trans} for trans in self.transitions]
        transitions = [{k: z3.substitute(trans[k], *list_mapping) for k in trans} for trans in self.transitions]
        branches = list(zip(conditions, transitions))
        return LoopRecurrence(initial, branches)

    def copy_rec_with_diff_initial(self, new_initial):
        branches = list(zip(self.conditions, self.transitions))
        return LoopRecurrence(new_initial, branches)

    def get_n_values_starts_with(self, start, n, start_value=None):
        return self.get_first_n_values(n, start, start_value)
        # if start_value is None:
        #     first_values, index_seq = self.get_first_n_values(start + n)
        #     return first_values[start:], index_seq[start:]
        # else:
        #     first_values, index_seq = self.get_first_n_values(n, start_value)
        #     return first_values, index_seq

    def get_first_n_values(self, n, start=0, start_value=None):
        assert(self.is_standard())
        first_n_values = [self.initial] if start_value is None else [start_value]
        conditions = self.conditions
        transitions = self.transitions
        index_seq = []
        for i in range(start, start + n):
            cur_values = first_n_values[-1]
            # cur_transitions = [{k.subs(self.ind_var, i): trans[k].subs(self.ind_var, i) for k in trans} for trans in transitions]
            # cur_conditions = [cond.subs(self.ind_var, i) for cond in conditions]
            cur_conditions = [z3.substitute(cond, (self.ind_var, z3.IntVal(i))) for cond in conditions]
            for cond_index, cond in enumerate(cur_conditions):
                # if z3.simplify(cond.subs(cur_values).simplify()) == z3.BoolVal(True):
                cur_val_mapping = list(cur_values.items())
                solver = z3.Solver()
                # if z3.simplify(z3.substitute(cond, *cur_val_mapping)) == z3.BoolVal(True):
                if solver.check(z3.Not(z3.substitute(cond, cur_val_mapping))) == z3.unsat:
                    # cur_transition = {k.subs(self.ind_var, i): v.subs(self.ind_var, i) for k, v in transitions[cond_index].items()}
                    # true_values = {k: v.subs(cur_values) for k, v in cur_transition.items()}
                    cur_transition = {z3.simplify(z3.substitute(k, (self.ind_var, z3.IntVal(i)))): z3.substitute(v, (self.ind_var, z3.IntVal(i))) for k, v in transitions[cond_index].items()}
                    true_values = {z3.simplify(k): z3.simplify(z3.substitute(v, *cur_val_mapping)) for k, v in cur_transition.items()}
                    first_n_values.append(true_values)
                    index_seq.append(cond_index)
                    break
        return first_n_values, index_seq

    def is_all_initialized(self):
        num_symbolic_values = len(self.get_symbolic_values())
        are_all_numbers = all([e.decl().kind() != z3.Z3_OP_UNINTERPRETED for e in self.initial.values()])
        all_initialized = all((func_decl(0) in self.initial for func_decl in self.func_decls))
        return num_symbolic_values == 0 and are_all_numbers and all_initialized
        # return len(self.get_symbolic_values()) == 0 and all([e.is_number for e in self.initial.values()]) and all((func_decl(0) in self.initial for func_decl in self.func_decls))

    def run_one_iteration_for_ith_transition(self, cur_value, ith):
        transition = self.transitions[ith]
        # left composition
        left_val = {v: z3.substitute(transition[v], list(cur_value.items())) for v in transition if z3.simplify(v) not in self.reverses}
        # right composition
        zero_reverses_mapping = [(z3.simplify(func_app.decl()(self.ind_var)), func_app.decl()(z3.IntVal(0))) for func_app in self.reverses]
        right_transition = {z3.simplify(func_app.decl()(0)): z3.substitute(trans, zero_reverses_mapping) for func_app, trans in transition.items() if z3.simplify(func_app) in self.reverses}
        shifted_reverses = [z3.simplify(func_app.decl()(self.ind_var)) for func_app in self.reverses]
        right_val = {z3.simplify(k.decl()(self.ind_var + 1)): z3.substitute(v, list(right_transition.items())) for k, v in cur_value.items() if z3.simplify(k) in shifted_reverses}
        val = {z3.simplify(z3.substitute(k, (self.ind_var, self.ind_var - 1))): v for k, v in (left_val | right_val).items()}
        return val

    def project_to_scalars(self):
        initial = self.initial
        scalar_funcs = [func.subs({self.ind_var: self.ind_var + 1}, simultaneous=True) for func in self.all_scalar_func]
        transitions = [{k: v for k, v in trans.items() if k in scalar_funcs} for trans in self.transitions]
        # branches = list(zip(self.conditions, transitions))
        removed = []
        conditions = []
        for i in range(len(transitions)):
            if i in removed: continue
            cond = self.conditions[i]
            for j in range(i + 1, len(transitions)):
                if j in removed: continue
                if utils.is_same_transition(transitions[i], transitions[j]):
                    removed.append(j)
                    cond = z3.Or(cond, self.conditions[j])
            conditions.append(z3.simplify(cond))
        filtered_transitions = [transitions[i] for i in range(len(transitions)) if i not in removed]
        assert(len(conditions) == len(filtered_transitions))
        branches = list(zip(conditions, filtered_transitions))
        return Recurrence(initial, branches)

    def project_to_arrays(self):
        initial = {}
        array_funcs = [func.subs({self.ind_var: self.ind_var + 1}, simultaneous=True) for func in self.all_array_func]
        transitions = [{k: v for k, v in trans.items() if k in array_funcs} for trans in self.transitions]
        branches = list(zip(self.conditions, transitions))
        return Recurrence(initial, branches)

    @property
    def closed_forms(self):
        return self._closed_forms.copy()

    @property
    def func_decls(self):
        return self._func_decls.copy()

    @property
    def all_funcs(self):
        funcs = reduce(set.union, [set(trans.keys()) for trans in self.transitions])
        # return [func.subs({self.ind_var: self.ind_var - 1}) for func in funcs]
        return [z3.substitute(func, (self.ind_var, self.ind_var - 1)) for func in funcs]

    @property
    def ind_var(self):
        return self._ind_var

    @ind_var.setter
    def ind_var(self, var):
        self._ind_var = var

    # @property
    # def base_conditions(self):
    #     return self._base_conditions.copy()

    # @property
    # def base_transitions(self):
    #     return self._base_transitions.copy()

    # @property
    # def recursive_conditions(self):
    #     return self._recursive_conditions.copy()

    # @property
    # def recursive_transitions(self):
    #     return self._recursive_transitions.copy()

    @property
    def conditions(self):
        return self._conditions.copy()
        # return self.base_conditions + self.recursive_conditions

    @property
    def transitions(self):
        return self._transitions.copy()
        # return self.base_transitions + self.recursive_transitions

    @property
    def initial(self):
        return self._initial.copy()

    @property
    def all_scalar_func(self):
        return [func for func in self.all_funcs if all([narg == 1 for narg in func.nargs])]

    @property
    def all_array_func(self):
        return [func for func in self.all_funcs if all([narg != 1 for narg in func.nargs])]

    def get_all_array_app(self, array_func):
        k = array_func.subs({self.ind_var: self.ind_var + 1}, simultaneous=True)
        res = []
        for trans in self.transitions:
            app = [i for i in trans[k].atoms(sp.Function) if isinstance(i, AppliedUndef) and i.func == array_func.func]
            assert(len(app) <= 1)
            if len(app) == 0:
                app = array_func
            else:
                app = app[0]
            res.append(app)
        return res

    @classmethod
    def build_nonconditional_from_rec_by_seq(cls, rec, seq, initial):
        transitions = rec.transitions
        ind_var = rec.ind_var
        func_decls = rec.func_decls
        transition = {z3.simplify(func_decl(ind_var + 1)): func_decl(ind_var) for func_decl in func_decls}
        for i in seq:
            cur_transition = transitions[i]
            # left composition
            left_cur_transition = {z3.simplify(func_app.decl()(ind_var)): trans for func_app, trans in cur_transition.items()}
            left_transition = {z3.simplify(func_app): z3.substitute(trans, *list(left_cur_transition.items())) for func_app, trans in transition.items() if z3.simplify(func_app) not in rec.reverses}

            # right composition
            right_cur_transition = cur_transition
            transition_shift = {z3.simplify(func_app.decl()(ind_var)): trans for func_app, trans in transition.items()}
            right_transition = {z3.simplify(func_app): z3.substitute(trans, *list(transition_shift.items())) for func_app, trans in right_cur_transition.items() if z3.simplify(func_app) in rec.reverses}
            transition = left_transition | right_transition
        new_rec = cls(initial, [(z3.BoolVal(True), transition)])
        new_rec.set_reverse = rec.reverses
        return new_rec

    def get_app(self):
        terms = self._get_app_from_conditions() | self._get_app_from_transitions()
        return terms

    def is_linear(self):
        app = self.get_app()
        for trans in self.transitions:
            if not all(utils.is_linear(expr, app) for expr in trans.values()):
                return False
        return True

    def get_symbolic_values(self):
        initial = self.initial
        # from_initial = reduce(set.union, [v.free_symbols for v in initial.values()], set())
        # from_condition = reduce(set.union, [cond.free_symbols for cond in self.conditions], set())
        from_initial = reduce(set.union, [utils.get_vars(v) for v in initial.values()], set())
        from_condition = reduce(set.union, [utils.get_vars(cond) for cond in self.conditions], set())
        _from_transition = reduce(set.union, [trans.values() for trans in self.transitions], set())
        # from_transition = reduce(set.union, [trans.free_symbols for trans in _from_transition], set())
        from_transition = reduce(set.union, [utils.get_vars(trans) for trans in _from_transition], set())
        return (from_initial | from_condition | from_transition) - {self.ind_var}

    def is_linear_conditional(self):
        terms = self.get_terms()

    def _get_all_func_decl(self):
        app_condition = self._get_app_from_conditions()
        app_trans = self._get_app_from_transitions()
        functions_initial = self._get_initial_func()
        # functions_cond_trans = {app.func for app in app_condition | app_trans if not app.is_Symbol}
        functions_cond_trans = {app.decl() for app in app_condition | app_trans if app.decl().kind() != z3.Z3_OP_UNINTERPRETED}
        transitions_keys = reduce(set.union, [set(trans.keys()) for trans in self.transitions])
        func_decls_from_keys = {k.decl() for k in transitions_keys}
        return functions_initial | functions_cond_trans | func_decls_from_keys

    def is_standard(self):
        '''Check whether this recurrence is in the standard form.
           By standard form, we mean in the transitions, left-hand sides are all of form f(..., n+1) and right-hand sides are all of form f(..., n).'''
        solver = z3.Solver()
        ind_var = self.ind_var
        for trans in self.transitions:
            for lhs, rhs in trans.items():
                decl = lhs.decl()
                if decl.kind() != z3.Z3_OP_UNINTERPRETED or decl.arity() < 1 or z3.simplify(lhs.children()[-1] - ind_var - 1 != 0):
                    return False
                rhs_app = {app for app in utils.get_app(rhs)}
                for app in rhs_app:
                    decl = app.decl()
                    if decl.kind() != z3.Z3_OP_UNINTERPRETED or decl.arity() < 1 or z3.simplify(app.children()[-1] - ind_var != 0):
                        return False
        return True

    def _get_app_from_conditions(self):
        app = reduce(set.union, [utils.get_app(cond) for cond in self.conditions])
        return app

    def _get_app_from_transitions(self):
        app = set()
        for trans in self.transitions:
            trans_app = reduce(set.union, [utils.get_app(expr) for expr in trans.values()], set())
            app = app | trans_app 
        return app

    def _get_initial_func(self):
        # return {app.func for app in self.initial if not app.is_Symbol}
        return {app.func for app in self.initial if app.decl().kind() != z3.Z3_OP_UNINTERPRETED}

    def sympify(self):
        exp = defaultdict(list)
        for cond, trans in zip(self.conditions, self.transitions):
            for f in trans:
                exp[f].append((utils.to_sympy(trans[f]), utils.to_sympy(cond)))
        return {f: sp.Piecewise(*exp[f]) for f in exp}

    def pprint(self):
        sp.init_printing()
        rec_sym = self.sympify()
        for f in self.initial:
            sp.pprint(sp.Eq(utils.to_sympy(f), utils.to_sympy(self.initial[f])))
        print()
        for f in rec_sym:
            sp.pprint(sp.Eq(utils.to_sympy(f), rec_sym[f]))
            print()

