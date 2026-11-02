import z3


def is_inductive_scalar_closed_form(rec, closed_form, precondition=None,
                                   timeout_ms=None, require_complete=False):
    try:
        closed_dict = closed_form.as_dict()
    except AttributeError:
        return not require_complete

    ind_var = rec.ind_var
    func_closed = {}
    for func_decl in rec.func_decls:
        if func_decl.arity() != 1:
            if require_complete:
                return False
            continue
        app = func_decl(ind_var)
        if app in closed_dict:
            func_closed[app] = closed_dict[app]
        elif require_complete:
            return False

    if not func_closed:
        return not require_complete
    if require_complete and rec.reverses:
        return False

    solver = z3.Solver(ctx=ind_var.ctx)
    if timeout_ms is not None:
        solver.set(timeout=timeout_ms)
    if precondition is not None:
        solver.add(precondition)
    for app, expr in func_closed.items():
        initial_app = app.decl()(z3.IntVal(0, ctx=ind_var.ctx))
        if initial_app not in rec.initial:
            if require_complete:
                return False
            continue
        initial_value = z3.simplify(z3.substitute(expr, (ind_var, z3.IntVal(0, ctx=ind_var.ctx))))
        solver.push()
        solver.add(initial_value != rec.initial[initial_app])
        result = solver.check()
        solver.pop()
        if result != z3.unsat:
            return False

    current_mapping = list(func_closed.items())
    for condition, transition in zip(rec.conditions, rec.transitions):
        closed_condition = z3.simplify(z3.substitute(condition, *current_mapping))
        for lhs_next, rhs in transition.items():
            if lhs_next.decl().arity() != 1:
                continue
            app = lhs_next.decl()(ind_var)
            if app not in func_closed:
                continue
            expected_next = z3.simplify(z3.substitute(func_closed[app], (ind_var, ind_var + 1)))
            actual_next = z3.simplify(z3.substitute(rhs, *current_mapping))
            solver.push()
            solver.add(ind_var >= 0, closed_condition, expected_next != actual_next)
            result = solver.check()
            solver.pop()
            if result != z3.unsat:
                return False

    return True
