import time
import z3
import fire
import sys
from functools import reduce
from sympy.core.function import AppliedUndef
sys.path.append('./rec_solver')
from rec_solver import solve_file
from rec_solver.core.closed_form import MultiFuncClosedForm, ExprClosedForm, SymbolicClosedForm, PiecewiseClosedForm
from rec_solver.core.utils import to_z3, get_applied_functions

def main(filename, inv_var):
    out_filename = "tmp/closed.smt2"
    closed = solve_file(filename)
    solver = z3.Solver()
    closed_dict = closed.as_dict()
    # apps = reduce(set.union, [k.atoms(AppliedUndef) | v.atoms(AppliedUndef) for k, v in closed_dict.items()])
    apps = reduce(set.union, [get_applied_functions(k) for k in closed_dict])
    # apps = reduce(set.union, [get_applied_functions(k) | get_applied_functions(v) for k, v in closed_dict.items()])

    # remove the last argument from the function applications
    remove_func_mapping = []
    for f in apps:
        assert(f.decl().arity() > 0)
        if f.decl().arity() == 1:
            remove_func_mapping.append((f, z3.Int(f.decl().name())))
        else:
            args = f.children()[:-1]
            new_func = z3.Function(f.decl().name(), *[arg.sort() for arg in args], z3.IntSort())
            remove_func_mapping.append((f, new_func(args)))

    # remove_func_mapping = [(f, z3.Int(f.decl().name())) for f in apps]
    # new_closed_dict = {k.subs(remove_func_mapping, simultaneous=True): v.subs(remove_func_mapping, simultaneous=True) for k, v in closed_dict.items()}
    new_closed_dict = {z3.substitute(k, remove_func_mapping): z3.substitute(v, remove_func_mapping) for k, v in closed_dict.items()}
    for k, e in new_closed_dict.items():
        solver.add(k == e)
    with open(out_filename, 'w') as fp:
        fp.write(solver.to_smt2())
    # print(closed.to_z3())

if __name__ == '__main__':
    fire.Fire(main)