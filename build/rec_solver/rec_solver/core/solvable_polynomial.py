import sympy as sp
import z3
from . import utils
from .recurrence import LoopRecurrence
from functools import reduce

def solve_solvable_map(rec: LoopRecurrence):
    components = get_layers_for_solvable_map(rec)
    for component in components:
        partial_closed_form = solve_solvable_map_for_component(rec, component)
        # print(rec.initial)
        # partial_closed_form = {k: c.subs(rec.initial, simultaneous=True) for k, c in partial_closed_form.items()}
        partial_closed_form = {k: z3.substitute(c, *list(rec.initial.items())) for k, c in partial_closed_form.items()}
        rec.simplify_with_partial_solution(partial_closed_form)
    return rec.closed_forms

def solve_solvable_map_for_component(rec: LoopRecurrence, component):
    ordered_component = list(component)
    transition = rec.transitions[0]
    func_decls = [function.decl() for function in ordered_component]
    keys = [z3.simplify(func(rec.ind_var + 1)) for func in func_decls]
    linear_parts = []
    other_parts = []
    for k in keys:
        expr = transition[k]
        linear, other = utils.split_linear_others(expr, ordered_component, rec.ind_var)
        linear_parts.append(utils.to_sympy(linear))
        other_parts.append(utils.to_sympy(other))
    linear_matrix_form, _ = sp.linear_eq_to_matrix(linear_parts, [utils.to_sympy(c) for c in ordered_component])
    matrix_eigenvals = linear_matrix_form.eigenvals()
    base_multiplicity_other_part = [utils.get_exponential_base_and_multiplicity(other, rec.ind_var) for other in other_parts]
    all_bases = set(matrix_eigenvals) | reduce(set.union, (set(base_mul.keys()) for base_mul in base_multiplicity_other_part), set())
    bases_multi_dict = {base: 0 for base in all_bases}
    for base in all_bases:
        for cur_dict in [matrix_eigenvals] + base_multiplicity_other_part:
            bases_multi_dict[base] += cur_dict.get(base, 0)
    n = sum(bases_multi_dict.values())
    first_n_values, _ = rec.get_first_n_values(n)
    eps = solve_for_exponential_polynomial(bases_multi_dict, func_decls, first_n_values, rec.ind_var)
    return eps

def solve_for_exponential_polynomial(bases_multi_dict, func_decls, first_n_values, ind_var):
    '''Here I delegate the linear equation solving to sympy.
       It had better to replace it with z3, if z3 has any internal equation solving technique which allows parameters'''
    template, coeffs = gen_exponential_polynomials_template(bases_multi_dict, ind_var)
    eps = {}
    deno = sp.Symbol('deno', integer=True)
    vars = [utils.to_sympy(c) for c in coeffs] + [deno]
    for func in func_decls:
        # solver = z3.Solver()
        eqs = []
        for i, value in enumerate(first_n_values):
            cur_func_value = value[func(i)]
            cur_func_value_sp = utils.to_sympy(cur_func_value)
            ind_var_sp = utils.to_sympy(ind_var)
            template_sp = utils.to_sympy(template)
            eqs.append(deno*cur_func_value_sp - template_sp.subs(ind_var_sp, i))
            # eqs.append(cur_func_value - z3.substitute(template, (ind_var, z3.IntVal(i))))
            # solver.add(cur_func_value == z3.simplify(z3.substitute(template, (ind_var, z3.IntVal(i)))))
        # sol = sp.solve(eqs, [utils.to_sympy(c) for c in coeffs] + [deno])
        sol = sp.linsolve(eqs, vars)
        if sol.is_empty:
            raise Exception('Empty')
        parametric_sol, = sol
        val = {}
        for var in vars:
            all_deno = [sp.fraction(s.coeff(var))[1] for s in parametric_sol]
            val[var] = sp.lcm(all_deno)
        instantiated_sol = {var: v.subs(val) for var, v in zip(vars, parametric_sol)}
        # sat_res = solver.check()
        # assert(sat_res == z3.sat)
        # print(sol)
        instantiated_ep = utils.to_sympy(template).subs(instantiated_sol)
        # m = solver.model()
        # instantiated_ep = m.eval(template)
        eps[func(ind_var)] = z3.simplify(utils.to_z3(instantiated_ep)/z3.IntVal(val[deno]))
    return eps

def gen_exponential_polynomials_template(bases_multi_dict, ind_var):
    template = z3.IntVal(0)
    unknowns = []
    for base, multi in bases_multi_dict.items():
        poly_template, coeffs = gen_polynomial_template_for_degree(ind_var, multi-1, str(base))
        if base == 0:
            # template += sp_poly_template*sp.Piecewise((1, sp.Eq(utils.to_sympy(ind_var), 0)), (0, True))
            template += poly_template*z3.If(ind_var == 0, 1, 0)
        else:
            # template += sp_poly_template*base**utils.to_sympy(ind_var)
            template += poly_template*z3_pow(base, ind_var)
        unknowns.extend(coeffs)
    return template, unknowns

def gen_polynomial_template_for_degree(ind_var, degr, name=""):
    # coeffs = sp.symbols('_%s_c:%d' % (name, degr + 1))
    # coeffs = [sp.Symbol('_%s_c%d' % (name, i)) for i in range(degr + 1)]
    name = name.replace("-", "__")
    coeffs = [z3.Int('_%s_c%d' % (name, i)) for i in range(degr + 1)]
    template = sum([c*z3_pow(ind_var, i) for i, c in enumerate(coeffs)], 0)
    return template, list(coeffs)

def z3_pow(expr, p):
    # assert(isinstance(p, int) or expr == 1 or expr == -1)
    # mod = z3.Function('Mod', z3.IntSort(), z3.IntSort(), z3.IntSort())
    if expr == 1: return expr
    if expr == -1 and not isinstance(p, int): return z3.If(p % 2 == 0, 1, -1)
    res = 1
    if isinstance(p, int):
        for _ in range(p):
            res *= expr
        return res
    z3_pow = z3.Function('Pow', z3.RealSort(), z3.IntSort(), z3.IntSort())
    base = utils.num2z3(expr)
    return z3_pow(expr, p)

def get_layers_for_solvable_map(rec: LoopRecurrence):
    # digraph, functions = build_adjacency_matrix(rec)
    # components = utils.sorted_strong_ly_connected_components(digraph)
    # return [{functions[i] for i in component} for component in components]
    components = []
    transition = rec.transitions[0]
    functions = [func(rec.ind_var) for func in rec.func_decls]
    considered = []
    while set(considered) != set(functions):
        cur_considered = []
        considered_yet = set(functions) - set(considered)
        for func in considered_yet:
            # func_next = func.subs({rec.ind_var: rec.ind_var + 1})
            func_next = z3.simplify(z3.substitute(func, (rec.ind_var, rec.ind_var + 1)))
            liner_part, poly_part = utils.split_linear_others(transition[func_next], functions, rec.ind_var)
            all_apps = utils.get_app(poly_part)
            if all_apps.issubset(set(considered)):
                cur_considered.append(func)
        considered.extend(cur_considered)
        components.append(cur_considered)
        # digraph = build_adjacency_matrix(rec, cur_considered)
        # cs = utils.sorted_strong_ly_connected_components(digraph)
        # print(cs)
        # print(cur_considered)
        # for c in cs:
        #     components.append([cur_considered[i] for i in c])
        #     considered.extend(components[-1])
    return components
        # considered.extend(cur_considered)

def build_adjacency_matrix(rec: LoopRecurrence, projection):
    transition = rec.transitions[0]
    digraph = sp.zeros(len(rec.func_decls))
    functions = [func(rec.ind_var) for func in rec.func_decls]
    for i, f_i in enumerate(projection):
        # f_n_1 = f_i.subs({rec.ind_var: rec.ind_var + 1})
        f_n_1 = z3.simplify(z3.substitute(f_i, (rec.ind_var, rec.ind_var + 1)))
        # _, other_expr = utils.split_linear_others(transition[f_n_1], functions, rec.ind_var)
        for j, f_j in enumerate(projection):
            # if f_j.func in utils.get_func_decls(other_expr):
            if f_j.decl() in utils.get_func_decls(transition[f_n_1]):
                digraph[i, j] = 1
    return digraph

def is_solvable_map(rec: LoopRecurrence):
    conditions = rec.conditions
    solver = z3.Solver()
    # if z3.simplify(conditions[0]) != sp.true: return False
    if solver.check(z3.Not(conditions[0])) != z3.unsat: return False
    ind_var = rec.ind_var
    func_decls = rec.func_decls
    if any((func.arity() > 1 for func in func_decls)): return False
    layers = get_layers_for_solvable_map(rec)
    transition = rec.transitions[0]
    functions = [func_decl(ind_var) for func_decl in rec.func_decls]
    for i, layer in enumerate(layers):
        functions_later_layers = reduce(set.union, layers[i:], set())
        for func in layer:
            f_n_1 = z3.simplify(func.decl()(ind_var + 1))
            _, other_expr = utils.split_linear_others(transition[f_n_1], functions, ind_var)
            apps = utils.get_app(other_expr) - {ind_var}
            if any((app in functions_later_layers for app in apps)):
                return False
    return True