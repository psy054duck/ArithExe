import z3
import sympy as sp
from sympy.utilities.iterables import strongly_connected_components
from . import utils
from collections import defaultdict
from .logic_simplification import DNFConverter, equals
from functools import reduce
# z3.set_option(max_depth=99999999, max_args=9999999, max_width=999999999, max_lines=99999999, max_indent=99999999)

class PeriodicClosedForm:
    def __init__(self, closed_form_list, ind_var, conditions=None):
        self._closed_form_list = closed_form_list
        self._ind_var = ind_var
        if conditions is not None:
            self._conditions = conditions
        else:
            self._conditions = [self.ind_var % self.period == i for i in range(self.period)]
    
    def eval_at(self, n):
        # if self.period == 1:
        #     r = 0
        # else:
        #     assert(n >= 0)
        #     r = n % self.period
        # if isinstance(n, int):
        #     n = z3.IntVal(n)
        # # val = {k.subs({self.ind_var: n}, simultaneous=True): c.subs({self.ind_var: n}, simultaneous=True) for k, c in self._closed_form_list[r].items()}
        # val = {z3.simplify(z3.substitute(k, (self.ind_var, n))): z3.substitute(c, (self.ind_var, n)) for k, c in self._closed_form_list[r].items()}
        raw_val = self.as_dict()
        if isinstance(n, int):
            n = z3.IntVal(n)
        val = {z3.simplify(z3.substitute(f, (self.ind_var, n))): z3.simplify(z3.substitute(v, (self.ind_var, n))) for f, v in raw_val.items()}
        return val

    @property
    def conditions(self):
        return self._conditions.copy()

    def sympify(self):
        sympified = defaultdict(list)
        for i in range(self.period):
            cond = utils.to_sympy(self.conditions[i])
            for f in self.closed_forms[i]:
                sympified[utils.to_sympy(f)].append((utils.to_sympy(self.closed_forms[i][f]), cond))
        res = {f: sp.Piecewise(*sympified[f]) for f in sympified}
        return res

    def as_dict(self):
        tmp_res = defaultdict(list)
        # for i in range(self.period):
        for i, cond in enumerate(self.conditions):
            # cond = z3.BoolVal(True)
            # if self.period != 1:
            #     cond = self.ind_var % self.period == i
            for f in self.closed_forms[i]:
                tmp_res[f].append((utils.sp_z3_simplify(self.closed_forms[i][f], cond), cond))
        return {z3.simplify(f): utils.to_ite(tmp_res[f]) for f in tmp_res}

    def pprint(self):
        sp.init_printing()
        sp_dict = self.sympify()
        for f in sp_dict:
            sp.pprint(sp.Eq(f, sp_dict[f]))
            print()

    def subs(self, mapping, reverse_list=[]):
        new_list = []
        normal_reverse_list = [app.decl()(self.ind_var) for app in reverse_list]
        zero_reverse_list = [app.decl()(z3.IntVal(0)) for app in reverse_list]
        is_initial_mapping = all(len(app.children()) == 1 and app.arg(0) == 0 for app in mapping)
        mapping = {k: v if z3.is_int(v) else z3.ToInt(v) for k, v in mapping.items()}
        for part in self._closed_form_list:
            # cur_closed = {k: c.subs(mapping, simultaneous=True) for k, c in part.items()}
            left_cur_closed = {k: z3.substitute(c, *list(mapping.items())) for k, c in part.items() if k not in normal_reverse_list}
            left_mapping = {k: v for k, v in mapping.items() if k not in normal_reverse_list}
            right_cur_closed = {}
            if is_initial_mapping and len(reverse_list) > 0:
                assert(all([k.decl().name() == v.decl().name() for k, v in mapping.items() if k in normal_reverse_list]))
                zero_closed = {k.decl()(z3.IntVal(0)): v for k, v in part.items()}
                initial_transform = {z3.Int(k.decl().name()): k.decl()(z3.IntVal(0)) for k in mapping if k in zero_reverse_list}
                normal_mapping = {k.decl()(self.ind_var): c for k, c in mapping.items()}
                zero_closed_mapping = [(k, v if z3.is_int(v) else z3.ToInt(v)) for k, v in zero_closed.items()]
                right_cur_closed = {k: z3.substitute(z3.substitute(c, list(initial_transform.items())), zero_closed_mapping) for k, c in normal_mapping.items() if k in normal_reverse_list}
                # right_cur_closed = {k: z3.substitute(z3.substitute(c, *list(initial_transform.items())), *list(zero_closed.items())) for k, c in normal_mapping.items() if k in normal_reverse_list}
                right_cur_closed = {k: z3.substitute(c, [(v, k) for k, v in initial_transform.items()]) for k, c in right_cur_closed.items()}
                right_cur_closed = {k: z3.substitute(c, *list(left_mapping.items())) for k, c in right_cur_closed.items()}
            cur_closed = left_cur_closed | right_cur_closed
            # cur_closed = {k: z3.substitute(c, *list(mapping.items())) for k, c in part.items()}
            new_list.append(cur_closed)
        new_conditions = [z3.substitute(cond, *list(mapping.items())) for cond in self.conditions]
        return PeriodicClosedForm(new_list, self.ind_var, new_conditions)

    def __str__(self):
        return str(self._closed_form_list)

    def get_rth_part_closed_form(self, r):
        return self._closed_form_list[r]

    def to_z3(self):
        return self.selected_to_z3(self.all_vars)

    def selected_to_z3(self, vars):
        # closed_form_z3_list = [self._to_z3(closed, vars) for closed in self._closed_form_list]
        # res = {}
        # for var in vars:
        #     expr_z3 = closed_form_z3_list[-1][var]
        #     for i, closed in enumerate(closed_form_z3_list[:-1]):
        #         expr_z3 = z3.If(self.ind_var % self.period == i, closed[var], expr_z3)
        #     res[var] = z3.simplify(expr_z3)
        raw_closed = self.as_dict()
        return {v: raw_closed[v] for v in vars}


    def _to_z3(self, closed, vars):
        res = {}
        for k in vars:
            c = closed[k]
            res[k] = c
        return res

    @property
    def period(self):
        return len(self._closed_form_list)

    @property
    def ind_var(self):
        return self._ind_var

    @property
    def all_vars(self):
        try:
            closed = self._closed_form_list[0]
            return set(closed.keys())
        except:
            return set()

    # @property
    # def conditions(self):
    #     return [i % self.period for i in range(self.period)]
    
    @property
    def closed_forms(self) -> list[dict]:
        return self._closed_form_list

    @property
    def flatten_conditions(self):
        return self.conditions
        # if self.period == 1:
        #     yield z3.BoolVal(True)
        # else:
        #     for i in range(self.period):
        #         yield self.ind_var % self.period == i

    @property
    def flatten_closed_forms(self):
        for closed in self._closed_form_list:
            yield closed

class PiecewiseClosedForm:
    # def __init__(self, conditions=[], closed_forms=[], ind_var=sp.Symbol('n', integer=True)):
    def __init__(self, ind_var, conditions=[], closed_forms=[]):
        self._conditions = conditions
        self._closed_forms = closed_forms
        self._ind_var = ind_var

    def concatenate(self, latter_closed, threshold):
        conditions = [z3.And(cond, self.ind_var < threshold) for cond in self.conditions]
        conditions = conditions + latter_closed.conditions
        new_closed_forms = self.closed_forms + latter_closed.closed_forms
        return PiecewiseClosedForm(self.ind_var, conditions, new_closed_forms)

    def eval_at(self, n):
        assert(n >= 0)
        if isinstance(n, int):
            n = z3.IntVal(n)
        solver = z3.Solver()
        fired = [solver.check(z3.Not(z3.substitute(c, (self.ind_var, n)))) == z3.unsat for c in self.conditions]
        assert(any(fired))
        which = fired.index(True)
        return self.closed_forms[which].subs({self.ind_var: n})

    def sympify(self):
        sim = z3.Then(z3.With('simplify', elim_ite=True), z3.Tactic('ctx-solver-simplify'), z3.Tactic('solve-eqs'))
        expressions = defaultdict(list)
        for cond, closed in zip(self.conditions, self.closed_forms):
            sp_closed = closed.sympify()
            for f in sp_closed:
                expressions[f].append((sp_closed[f], utils.to_sympy(cond)))
        res = {f: sp.Piecewise(*expressions[f]) for f in expressions}
        return res

    def pprint(self):
        sp.init_printing()
        sp_dict = self.sympify()
        for f in sp_dict:
            sp.pprint(sp.simplify(sp.Eq(f, sp_dict[f])))
            print()

    def subs(self, mapping):
        # conditions = [c.subs(mapping, simultaneous=True) for c in self.conditions]
        conditions = [z3.substitute(c, *tuple(mapping.items())) for c in self.conditions]
        closed_forms = [c.subs(mapping) for c in self.closed_forms]
        return PiecewiseClosedForm(self.ind_var, conditions, closed_forms)

    def simple_subs(self, mapping):
        if len(mapping) == 0:
            return PiecewiseClosedForm(self.ind_var, self.conditions, self.closed_forms)
        mapping_list = list(mapping.items())
        conditions = [z3.substitute(c, *mapping_list) for c in self.conditions]
        # closed_forms = [z3.substitute(c, *mapping_list) for c in self.closed_forms]
        closed_forms = [c.subs(mapping) for c in self.closed_forms]
        return PiecewiseClosedForm(self.ind_var, conditions, closed_forms)

    def as_dict(self):
        closed_forms_z3 = [closed.as_dict() for closed in self.closed_forms]
        res = {}
        for var in self.all_vars:
            var_z3 = var
            expr_z3 = closed_forms_z3[-1][var_z3]
            # for i, cond in enumerate(self.conditions[:-1]):
            for closed, cond in reversed(list(zip(closed_forms_z3[:-1], self.conditions[:-1]))):
                # closed = closed_forms_z3[i][var_z3]
                expr_z3 = z3.If(cond, closed[var], expr_z3)
            res[var_z3] = z3.simplify(expr_z3)
        return res

    def __str__(self):
        res = ''
        max_prefix_len = max([len(str(cond)) for cond in self.conditions])
        for cond, closed in zip(self.conditions, self.closed_forms):
            res += '{0:{width}}: '.format(str(cond), width=max_prefix_len)
            res += str(closed) + '\n'
        return res

    def _compute_intervals(self, thresholds):
        res = []
        for i in range(len(thresholds) - 1):
            left = thresholds[i]
            right = thresholds[i + 1]
            res.append(sp.Interval(left, right, left_open=False, right_open=True))
        return res

    @property
    def closed_forms(self) -> list[PeriodicClosedForm]:
        return self._closed_forms

    @property
    def ind_var(self):
        return self._ind_var

    @property
    def conditions(self):
        return self._conditions

    @property
    def all_vars(self):
        try:
            return self.closed_forms[0].all_vars
        except:
            return set()

    @property
    def flatten_conditions(self):
        for case, closed in zip(self._conditions, self._closed_forms):
            for cond in closed.flatten_conditions:
                yield z3.And(case, cond)

    @property
    def flatten_closed_forms(self):
        for closed in self._closed_forms:
            for c in closed.flatten_closed_forms:
                yield c

    def to_z3(self):
        expressions = defaultdict(list)
        for cond, closed in zip(self.conditions, self.closed_forms):
            sp_closed = closed.to_z3()
            for f in sp_closed:
                expressions[f].append((sp_closed[f], cond))
        res = {f: utils.to_ite(expressions[f]) for f in expressions}
        return res

class SymbolicClosedForm:
    def __init__(self, constraints, closed_forms, ind_var):
        self._constraints = constraints
        self._closed_forms = closed_forms
        self._ind_var = ind_var
        self._reorder()
        # self._simplify_constraints()
        self._flatten_conditions = None
        self._flatten_closed_forms = None
        self._simplify()
        # assert(self._check_consistant())

    def _reorder(self):
        cond_lengths = [len(str(cond)) for cond in self._constraints]
        longest = max(cond_lengths)
        longest_idx = cond_lengths.index(longest)
        self._constraints[longest_idx], self._constraints[-1] = self._constraints[-1], self._constraints[longest_idx]
        self._closed_forms[longest_idx], self._closed_forms[-1] = self._closed_forms[-1], self._closed_forms[longest_idx]

    def _simplify(self):
        merged = defaultdict(set)
        conditions = self.flatten_conditions
        closed_forms = self.flatten_closed_forms
        for i, (cond_replaced, closed_replaced) in enumerate(zip(conditions, closed_forms)):
            for j, closed_match in enumerate(closed_forms):
                # if i == j or any((j in s) for s in merged.values()): continue
                if i == j: continue
                if all(equals(v == closed_replaced[v], v == closed_match[v], cond_replaced) for v in closed_replaced):
                    merged[j].add(i)
        V = list(range(len(conditions)))
        E = sum([[(i, j) for j in merged[i]] for i in merged], [])
        res_conditions = []
        res_closed_forms = []
        converter = DNFConverter()
        for component in strongly_connected_components((V, E)):
            any_idx = component[0]
            cur_cond = z3.Or([conditions[i] for i in component])
            simplified = z3.Or([z3.And(c) for c in converter.to_dnf(cur_cond)])
            simplified = z3.Or([z3.And(c) for c in converter.to_dnf(simplified)])
            cur_closed = closed_forms[any_idx]
            res_conditions.append(z3.simplify(simplified))
            res_closed_forms.append(cur_closed)
        self._flatten_conditions = res_conditions
        self._flatten_closed_forms = res_closed_forms

    def _check_consistant(self):
        solver = z3.Solver()
        res = solver.check(z3.Not(z3.Or(*self._constraints)))
        return res == z3.unsat

    def _simplify_constraints(self):
        self._constraints[-1] = z3.Not(z3.Or(False, *self._constraints[:-1]))
        new_constraints = []
        for constraint in self._constraints:
            dnf_converter = DNFConverter()
            new_constraint = z3.Or([z3.And(c) for c in dnf_converter.to_dnf(constraint)])
            new_constraints.append(new_constraint)
        self._constraints = new_constraints

    def __str__(self):
        res = ''
        for constraint, closed in zip(self._constraints, self._closed_forms):
            res += str(constraint) + ':\n'
            res += '\t' + str(closed)
        return res

    def sympify(self):
        expressions = defaultdict(list)
        sim = z3.Tactic('ctx-solver-simplify')
        for cond, closed in zip(self._constraints, self._closed_forms):
            sp_closed = closed.sympify()
            simplified_cond = z3.simplify(z3.Or(*[z3.And(*conjunct) for conjunct in sim(cond)]))
            sp_cond = utils.to_sympy(z3.simplify(simplified_cond))
            assert('If' not in str(sp_cond))
            # sp_cond = sp.parse_expr(str(z3.simplify(cond)), local_dict={'If': utils.ite2piecewise})
            if sp_cond is True:
                sp_cond = sp.true
            to_integer_dict = {}
            for symbol in sp_cond.free_symbols:
                to_integer_dict[symbol] = sp.Symbol(symbol.name, integer=True)
            sp_cond = sp_cond.subs(to_integer_dict, simultaneous=True)
            for f in sp_closed:
                expressions[f].append((sp_closed[f].subs(to_integer_dict, simultaneous=True), sp_cond))
        for f in expressions:
            last_closed, _ = expressions[f][-1]
            expressions[f][-1] = (last_closed, True)
        # return {f: sp.piecewise_exclusive(sp.piecewise_fold(sp.Piecewise(*expressions[f]))) for f in expressions}
        return {f: sp.piecewise_fold(sp.Piecewise(*expressions[f])) for f in expressions}

    def pprint(self):
        sp.init_printing()
        sp_dict = self.sympify()
        for f in sp_dict:
            # print(sp_dict[f])
            # if isinstance(sp_dict[f], sp.Piecewise):
            #     for arg in sp_dict[f].args:
            #         print(arg)
            assert('If' not in str(sp_dict[f]))
            # TODO there is a deep bug here, the two calls to to_sympy is tentative. should trace the sympify of all closed_forms to see which one fail to sympyfy If
            sp.pprint(sp.simplify(sp.Eq(f, sp_dict[f])))
            print()

    def subs(self, mapping):
        constraints = [z3.substitute(c, *[(k, v) for k, v in mapping.items()]) for c in self._constraints]
        closed_forms = [c.subs(mapping) for c in self._closed_forms]
        return SymbolicClosedForm(constraints, closed_forms, self.ind_var)

    def as_dict(self):
        res = {}
        for var in self.all_vars:
            var_z3 = var
            expr = self._closed_forms[-1].as_dict()[var_z3]
            for constraint, closed in zip(self._constraints[:-1], self._closed_forms[:-1]):
                expr = z3.If(constraint, closed.as_dict()[var_z3], expr)
            res[var_z3] = z3.simplify(expr)
        return res

    @property
    def ind_var(self):
        return self._ind_var

    @property
    def conditions(self):
        conditions = []
        for cons, piece_closed in zip(self._constraints, self._closed_forms):
            for piece_cond, periodic_closed in zip(piece_closed.conditions, piece_closed.closed_forms):
                period = periodic_closed.period
                for i in range(period):
                    cond = sp.sympify(str(cons)) & piece_cond & (self.ind_var % period == i)
                    conditions.append(sp.simplify(cond))
        return conditions

    @property
    def transitions(self):
        closed_forms = []
        for piece_closed in self.closed_forms:
            for periodic_closed in piece_closed.closed_forms:
                closed_forms.extend(periodic_closed.closed_forms)
        return closed_forms
                
    @property
    def closed_forms(self) -> list[PiecewiseClosedForm]:
        # return self._closed_forms
        return sum([closed.closed_forms for closed in self._closed_forms], [])


    @property
    def cases(self):
        # return self._constraints
        cases = []
        assert(len(self._constraints) == len(self._closed_forms))
        for cond_sym, closed in zip(self._constraints, self._closed_forms):
            for form, cond_piece in zip(closed.closed_forms, closed.conditions):
                cases.append((form, z3.And(cond_sym, cond_piece)))
        return cases

    @property
    def all_vars(self):
        try:
            return self._closed_forms[0].all_vars
        except:
            return set()

    @property
    def flatten_conditions(self):
        if self._flatten_conditions is not None:
            return self._flatten_conditions

        res = []
        for case, closed in zip(self._constraints, self._closed_forms):
            for cond in closed.flatten_conditions:
                res.append(z3.And(case, cond))
        self._flatten_conditions = res
        return res

    @property
    def flatten_closed_forms(self):
        if self._flatten_closed_forms is not None:
            return self._flatten_closed_forms

        res = []
        for closed in self._closed_forms:
            for c in closed.flatten_closed_forms:
                res.append(c)
        self._flatten_closed_forms = res
        return res

class ExprClosedForm:
    def __init__(self, closed_forms, ind_var):
        self._closed_forms = closed_forms
        self._ind_var = ind_var

    def is_trivial(self):
        if len(self._closed_forms) == 1:
            v = list(self._closed_forms.keys())[0]
            if v == 1:
                return True
        return False

    def __str__(self):
        res = ''
        for expr, c in self._closed_forms.items():
            res += '%s: %s\n' % (expr, c)
        return res

    def subs(self, mapping):
        closed_forms = [c.subs(mapping) for c in self._closed_forms]
        return ExprClosedForm(closed_forms)

    def to_z3(self):
        res = {utils.to_z3(expr): utils.to_z3(c) for expr, c in self._closed_forms.items()}
        return res

    def as_dict(self):
        return self._closed_forms.copy()

    def sympify(self):
        return {expr: c for expr, c in self._closed_forms.items()}

class MultiFuncClosedForm:
    def __init__(self, func_decl, closed_form):
        self._func_decl = func_decl
        self._closed_form = z3.simplify(closed_form)

    @property
    def func_decl(self):
        return self._func_decl

    def sympify(self):
        return {self.func_decl: self.closed_form}

    @property
    def closed_form(self):
        return self._closed_form

    def to_z3(self):
        return {self.func_decl: self._closed_form}

    def __str__(self):
        return "%s: %s" % (self.func_decl, self.closed_form)

    def as_dict(self):
        return self.to_z3()
