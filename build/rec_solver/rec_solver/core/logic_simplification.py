'''This module contains the logic simplification functions.'''
from typing import List, Tuple, Set, Optional
from itertools import product

import z3
from itertools import product

def equals(f1, f2, case=z3.BoolVal(True)):
    '''Check if two expressions are logically equivalent under the case.'''
    left2right = implies(f1, f2, case)
    right2left = implies(f2, f1, case)
    return left2right and right2left

def implies(f1, f2, case=z3.BoolVal(True)):
    '''Check if f1 implies f2 under the case.'''
    solver = z3.Solver()
    solver.add(case)
    return solver.check(f1, z3.Not(f2)) == z3.unsat

def is_valid(f):
    solver = z3.Solver()
    return solver.check(f) == z3.sat

def is_tautology(f):
    return equals(f, z3.BoolVal(True))

def is_contradiction(f):
    return equals(f, z3.BoolVal(False))

def is_literal(f):
    return is_atom(f) or z3.is_not(f) and is_atom(f.arg(0))

def get_vars(f: z3.ExprRef):
    '''f: a formula in z3
       ret: set of all variables in f'''
    r = set()
    def collect(f):
      if z3.is_const(f): 
          if f.decl().kind() == z3.Z3_OP_UNINTERPRETED:
              r.add(f)
      else:
          for c in f.children():
              collect(c)
    collect(f)
    return r

def is_atom(t):
    if not z3.is_bool(t):
        return False
    if not z3.is_app(t):
        return False
    k = t.decl().kind()
    if k == z3.Z3_OP_AND or k == z3.Z3_OP_OR or k == z3.Z3_OP_IMPLIES:
        return False
    if k == z3.Z3_OP_EQ and z3.is_bool(t.arg(0)):
        return False
    if k == z3.Z3_OP_TRUE or k == z3.Z3_OP_FALSE or k == z3.Z3_OP_XOR or k == z3.Z3_OP_NOT:
        return False
    return True

def atoms(fml):
    visited = set([])
    atms = set([])
    def atoms_rec(t, visited, atms):
        if t in visited:
            return
        visited |= { t }
        if is_atom(t):
            atms |= { t }
        for s in t.children():
            atoms_rec(s, visited, atms)
    atoms_rec(fml, visited, atms)
    return atms

def atom2literal(m, a):
    if z3.is_true(m.eval(a)):
        return a
    return z3.Not(a)

def implicant(atoms, s, snot):
    m = snot.model()
    lits = [atom2literal(m, a) for a in atoms]
    is_sat = s.check(lits)
    assert is_sat == z3.unsat
    core = s.unsat_core()
    return z3.Or([z3.mk_not(c) for c in core])

def to_cnf(fml):
    str(fml)
    atms = atoms(fml)
    s = z3.Solver()
    snot = z3.Solver()
    snot.add(z3.Not(fml))
    s.add(fml)

    while z3.sat == snot.check():
        clause = implicant(atms, s, snot)
        yield clause
        snot.add(clause)

def to_dnf(fml):
    not_fml = z3.Not(fml)
    cnf = list(to_cnf(not_fml))
    dnf = []
    for clause in cnf:
        literals = clause.children()
        dnf.append([z3.Not(lit) for lit in literals])
    return dnf

def atom2canonical(atom):
    '''Convert atom to a canonical form.'''
    if z3.is_lt(atom):
        return atom.arg(1) - atom.arg(0) - 1 >= 0
    elif z3.is_le(atom):
        return atom.arg(1) - atom.arg(0) >= 0
    elif z3.is_gt(atom):
        return atom.arg(0) - atom.arg(1) - 1 >= 0
    elif z3.is_ge(atom):
        return atom.arg(0) - atom.arg(1) >= 0
    elif z3.is_eq(atom):
        return atom.arg(0) - atom.arg(1) == 0

def literal2canonical(literal):
    '''Convert literal to a canonical form.'''
    if not z3.is_not(literal):
        return [atom2canonical(literal)]
    atom = literal.arg(0)
    if z3.is_lt(atom):
        return [atom2canonical(atom.arg(0) >= atom.arg(1))]
    elif z3.is_le(atom):
        return [atom2canonical(atom.arg(0) > atom.arg(1))]
    elif z3.is_gt(atom):
        return [atom2canonical(atom.arg(0) <= atom.arg(1))]
    elif z3.is_ge(atom):
        return [atom2canonical(atom.arg(0) < atom.arg(1))]
    elif z3.is_eq(atom):
        return [atom2canonical(atom.arg(0) > atom.arg(1)), atom2canonical(atom.arg(0) < atom.arg(1))]

class DNFConverter:
    def __init__(self):
        pass

    def to_dnf(self, fml):
        return to_dnf(fml)

def merge_conjunctions(conjunction, assumption=z3.BoolVal(True)):
    '''simplify conjunction by checking if some conjunct is implied by the rest.
       do it recursively until fix point.'''
    for i, conjunct in enumerate(conjunction):
        rest_conjunctions = [c for j, c in enumerate(conjunction) if j != i]
        rest = z3.And(rest_conjunctions)
        if implies(rest, conjunct, case=assumption):
            return merge_conjunctions(rest_conjunctions)
    return conjunction

def my_simplify(expr: z3.ExprRef, assumption=z3.BoolVal(True)) -> z3.ExprRef:
    """
    Simplify a Z3 expression by converting to CNF and removing entailed clauses.
    
    Args:
        expr: The Z3 expression to simplify
        assumption: Optional assumption to add to the solver
    
    Returns:
        Simplified Z3 expression
    """
    new_expr = z3.simplify(expr)
    clauses = to_cnf(new_expr)
    solver = z3.Solver()
    
    solver.add(assumption)
    
    remains = []
    
    for clause in clauses:
        solver.push()
        solver.add(z3.Not(clause))
        if solver.check() == z3.sat:
            # This clause is not entailed, so keep it
            neg = [z3.Not(c) for c in clause.children()]
            simplified_neg = merge_conjunctions(neg, assumption=assumption)
            simplified_clause = z3.Or([z3.Not(lit) for lit in simplified_neg])
            remains.append(z3.simplify(simplified_clause))
        solver.pop()
    
    return z3.simplify(z3.And(list(to_cnf(z3.simplify(z3.And(remains))))))

def merge_cases(conditions, expressions, precondition=z3.BoolVal(True)):
    """
    Merge cases with equivalent expressions under their respective conditions.
    
    Args:
        conditions: List of Z3 boolean expressions (conditions)
        expressions: List of Z3 expressions (corresponding expressions)
    
    Returns:
        Tuple of (merged_conditions, merged_expressions)
    """
    import z3
    
    def conditionally_eq(cond, e1, e2):
        """Check if e1 and e2 are equivalent under the condition cond"""
        solver = z3.Solver()
        solver.set("timeout", 3000)  # 3 seconds timeout
        solver.add(cond)
        solver.add(precondition)
        solver.add(e1 != e2)
        return solver.check() == z3.unsat
    
    pivot = 0
    res_conditions = conditions.copy()
    res_expressions = expressions.copy()
    
    while pivot < len(res_expressions):
        merged = set()
        pivot_expr = res_expressions[pivot]
        pivot_cond = res_conditions[pivot]
        
        # Iterate through to check if some case is equivalent to pivot expression
        for i in range(len(res_expressions)):
            if i == pivot:  # skip pivot
                continue
            if i in merged:  # skip already merged
                continue
            
            if conditionally_eq(res_conditions[i], pivot_expr, res_expressions[i]):
                pivot_cond = z3.Or(pivot_cond, res_conditions[i])
                merged.add(i)
        
        new_conditions = []
        new_expressions = []
        new_pivot_location = 0
        
        for i in range(len(res_conditions)):
            if i in merged:  # skip merged
                continue
            
            if i == pivot:
                new_pivot_location = len(new_conditions)
                new_conditions.append(my_simplify(pivot_cond, precondition))
            else:
                new_conditions.append(my_simplify(res_conditions[i], precondition))
            
            new_expressions.append(res_expressions[i])
        
        if merged:  # if we merged something
            pivot = new_pivot_location + 1
        else:
            pivot += 1
        
        res_conditions = new_conditions
        res_expressions = new_expressions
    
    return res_conditions, res_expressions

from itertools import product

def cartesian_product(num_cases: List[int]) -> List[List[int]]:
    """Generate cartesian product of indices for each case."""
    ranges = [range(n) for n in num_cases]
    return [list(indices) for indices in product(*ranges)]

def aux_expr2piecewise(expr: z3.ExprRef, cur_cond: z3.BoolRef, 
                      conditions: List[z3.BoolRef], expressions: List[z3.ExprRef]) -> None:
    """
    Auxiliary function to convert expression to piecewise form.
    
    Args:
        expr: The Z3 expression to convert
        cur_cond: Current condition context
        conditions: List to collect conditions (modified in-place)
        expressions: List to collect expressions (modified in-place)
    """
    # Base case: if the expression is a constant or a variable, we can directly add it
    if expr.decl().kind() == z3.Z3_OP_UNINTERPRETED and expr.num_args() == 0:
        # This is a constant or variable
        conditions.append(cur_cond)
        expressions.append(expr)
    elif expr.decl().kind() == z3.Z3_OP_ITE:
        # If the expression is an if-then-else, we can extract the conditions and expressions
        cond = expr.arg(0)
        then_expr = expr.arg(1)
        else_expr = expr.arg(2)
        
        aux_expr2piecewise(then_expr, z3.And(cond, cur_cond), conditions, expressions)
        aux_expr2piecewise(else_expr, z3.And(z3.Not(cond), cur_cond), conditions, expressions)
    
    elif expr.num_args() > 0:  # expr.is_app() equivalent
        arity = expr.num_args()
        all_conditions = []
        all_expressions = []
        num_cases = []
        
        for i in range(expr.num_args()):
            arg = expr.arg(i)
            cur_conditions, cur_expressions = expr2piecewise(arg)
            all_conditions.append(cur_conditions)
            all_expressions.append(cur_expressions)
            num_cases.append(len(cur_conditions))
        
        for indices in cartesian_product(num_cases):
            acc_condition = z3.BoolVal(True)
            acc_expressions = []
            
            for i in range(len(indices)):
                cur_condition = all_conditions[i][indices[i]]
                acc_condition = z3.And(acc_condition, cur_condition)
                acc_expressions.append(all_expressions[i][indices[i]])
            
            conditions.append(z3.And(acc_condition, cur_cond))
            
            # Reconstruct the expression with the new arguments
            if len(acc_expressions) == 1:
                new_expr = expr.decl()(acc_expressions[0])
            elif len(acc_expressions) == 2:
                new_expr = expr.decl()(acc_expressions[0], acc_expressions[1])
            else:
                # For more than 2 arguments, we need to handle differently
                new_expr = expr.decl()(*acc_expressions)
            
            expressions.append(new_expr)
    else:
        # Leaf node (constant or numeral)
        conditions.append(cur_cond)
        expressions.append(expr)

def expr2piecewise(expr: z3.ExprRef) -> Tuple[List[z3.BoolRef], List[z3.ExprRef]]:
    """
    Convert a Z3 expression to piecewise form.
    
    Args:
        expr: The Z3 expression to convert
    
    Returns:
        Tuple of (conditions, expressions) representing the piecewise function
    """
    conditions = []
    expressions = []
    
    aux_expr2piecewise(expr, z3.BoolVal(True), conditions, expressions)
    
    # Eliminate ITE from conditions
    eliminated_conditions = []
    for cond in conditions:
        eliminated_conditions.append(eliminate_ite(cond))
    
    return merge_cases(eliminated_conditions, expressions)

def eliminate_ite(expr: z3.ExprRef) -> z3.ExprRef:
    """
    Eliminate if-then-else terms from a Z3 expression using tactics.
    
    Args:
        expr: The Z3 expression to process
    
    Returns:
        Expression with ITE terms eliminated
    """
    # Create goal and add the expression
    goal = z3.Goal()
    goal.add(expr)
    
    # Apply the eliminate-term-ite tactic
    elim_ite_tactic = z3.Tactic("elim-term-ite")
    elim_res = elim_ite_tactic(goal)
    
    # Collect results into a list for OR operation
    to_or = []
    for i in range(len(elim_res)):
        to_or.append(elim_res[i].as_expr())
    
    # Create OR of all results
    if len(to_or) == 0:
        eliminated_expr = z3.BoolVal(False)
    elif len(to_or) == 1:
        eliminated_expr = to_or[0]
    else:
        eliminated_expr = z3.Or(to_or)
    
    # Apply quantifier elimination tactic
    elim_temp_tactic = z3.Tactic("qe")
    aux_vars = list(collect_aux_vars(eliminated_expr))
    
    if len(aux_vars) > 0:
        # If there are auxiliary variables, we need to eliminate them
        exists_expr = z3.Exists(aux_vars, eliminated_expr)
        qe_goal = z3.Goal()
        qe_goal.add(exists_expr)
        qe_res = elim_temp_tactic(qe_goal)
        
        qe_conditions = []
        for j in range(len(qe_res)):
            qe_conditions.append(qe_res[j].as_expr())
        
        if len(qe_conditions) == 0:
            eliminated_expr = z3.BoolVal(False)
        elif len(qe_conditions) == 1:
            eliminated_expr = qe_conditions[0]
        else:
            eliminated_expr = z3.Or(qe_conditions)
        
        eliminated_expr = z3.simplify(eliminated_expr)
        
        # Verify equivalence (assertion)
        assert is_equivalent(eliminated_expr, expr), "Elimination changed semantics"
    
    return z3.simplify(eliminated_expr)

def is_equivalent(expr1: z3.ExprRef, expr2: z3.ExprRef) -> bool:
    """
    Check if two Z3 expressions are logically equivalent.
    
    Args:
        expr1: First Z3 expression
        expr2: Second Z3 expression
    
    Returns:
        True if the expressions are equivalent, False otherwise
    """
    return implies(expr1, expr2) and implies(expr2, expr1)

def collect_aux_vars(expr: z3.ExprRef) -> Set[z3.ExprRef]:
    """
    Collect auxiliary variables from a Z3 expression.
    
    Args:
        expr: The Z3 expression to analyze
    
    Returns:
        Set of auxiliary variables in the expression
    """
    aux_vars = {var for var in get_vars(expr) if '!' in str(var)}
    return aux_vars

def piecewise2ite(conditions: List[z3.BoolRef], expressions: List[z3.ExprRef]) -> z3.ExprRef:
    """
    Convert piecewise conditions and expressions to a nested if-then-else expression.
    
    Args:
        conditions: List of Z3 boolean conditions
        expressions: List of Z3 expressions corresponding to each condition
    
    Returns:
        A Z3 if-then-else expression representing the piecewise function
    
    Raises:
        ValueError: If conditions and expressions have different sizes
    """
    if len(conditions) != len(expressions):
        raise ValueError("Conditions and expressions must have the same size.")
    
    if len(conditions) == 1:
        return expressions[0]
    
    # Merge cases to simplify the piecewise function
    new_conditions, new_expressions = merge_cases(conditions, expressions)
    
    # Build the if-then-else expression from right to left
    ite_expr = new_expressions[-1]  # Start with the last expression
    
    # Work backwards through the conditions
    for i in range(len(new_conditions) - 2, -1, -1):
        ite_expr = z3.If(new_conditions[i], new_expressions[i], ite_expr)
    
    return ite_expr

if __name__ == '__main__':
    from z3 import *
    c, z = Ints('c z')
    q0, q1, q2 = Ints('q0 q1 q2')
    conjunction = [c >= 1, c >= 0, z >= 3, z <= 4, z + c >= 4]
    disjunction = [[c >= 1, c >= 0], [c <= 2]]
    converter = DNFConverter()
    testing = z3.And(c + z >= 1, c + z <= 1, z3.Or(c >= 0, z <= 0), c >= -20)
    # print(converter.simplify_and(conjunction))
    # print(converter.simplify_or(disjunction))
    simplified = z3.simplify(converter.disjunction2z3(converter.to_dnf(testing)))
    print(simplified)
    print(equals(testing, simplified))
    # print(converter.to_dnf(z3.Or([z3.And(d) for d in disjunction])))
    cond = And(q0 >= 1,
        q1 >= 1,
        q2 >= 1,
        0 >= -1*q2 + -1*q1 + -1*q0 + z,
        Not(And(-1*z + c <= -1,
                Or(Not(q0 >= 1),
                   Not(q1 >= 1),
                   Not(q2 >= 1),
                   -1*q0 + z + -1*c <= 0,
                   And(q1 + q0 + -1*z + c <= -1,
                       -1*q2 + -1*q1 + -1*q0 + z + -1*c <= 0),
                   And(q0 + -1*z + c <= -1,
                       -1*q1 + -1*q0 + z + -1*c <= 0,
                       c <= -1),
                   And(q2 + q1 + q0 + -1*z + c <= -1,
                       Not(0 >= -1*q2 + -1*q1 + -1*q0 + z))))),
        Not(And(-1*z + c <= -2,
                Or(Not(q0 >= 1),
                   Not(q1 >= 1),
                   Not(q2 >= 1),
                   And(-1*q0 + z + -1*c <= 1, c <= -2),
                   And(q0 + -1*z + c <= -2,
                       -1*q1 + -1*q0 + z + -1*c <= 1),
                   And(q1 + q0 + -1*z + c <= -2,
                       -1*q2 + -1*q1 + -1*q0 + z + -1*c <= 1,
                       c <= -2),
                   And(q2 + q1 + q0 + -1*z + c <= -2,
                       Not(0 >= -1*q2 + -1*q1 + -1*q0 + z))))),
        Not(And(-1*q2 + -1*q1 + -1*q0 <= -1,
                Or(Not(q0 >= 1),
                   Not(q1 >= 1),
                   Not(q2 >= 1),
                   And(-1*q2 + -1*q1 <= -1,
                       q2 <= 0,
                       Or(-1*z + c + q2 + q1 + q0 <= -2,
                          z + -1*c + -1*q2 + -1*q1 + -1*q0 <= -1,
                          z + -1*q2 + -1*q1 + -1*q0 <= -1)),
                   And(q2 + q1 <= 0,
                       Or(z + -1*q2 + -1*q1 + -1*q0 <= -1,
                          And(-1*z + c + q2 + q1 + q0 <= -1,
                              -1*c + z + -1*q2 + -1*q1 + -1*q0 <=
                              0))),
                   And(q2 >= 1,
                       Or(z + -1*q2 + -1*q1 + -1*q0 <= -1,
                          And(-1*z + c + q2 + q1 + q0 <= -1,
                              -1*c + z + -1*q2 + -1*q1 + -1*q0 <=
                              0)))))),
        Not(And(q0 >= 1,
                Or(Not(q0 >= 1),
                   Not(q1 >= 1),
                   z + -1*q0 <= -1,
                   Not(q2 >= 1),
                   And(q1 <= -1,
                       -1*q2 + -1*q1 <= -1,
                       Or(z + -1*q0 <= -1,
                          And(-1*z + c + q0 <= -1,
                              -1*c + z + -1*q0 <= 0))),
                   And(q2 + q1 <= -1,
                       Not(0 >= -1*q2 + -1*q1 + -1*q0 + z)),
                   And(-1*z + c + q0 <= -1,
                       -1*c + z + -1*q0 <= 0)))),
        Not(And(-1*q1 + -1*q0 <= -1,
                Or(Not(q0 >= 1),
                   Not(q1 >= 1),
                   Not(q2 >= 1),
                   And(q2 <= -1,
                       Not(0 >= -1*q2 + -1*q1 + -1*q0 + z)),
                   And(q1 >= 1,
                       Or(z + -1*q1 + -1*q0 <= -1,
                          z + -1*c + -1*q1 + -1*q0 <= -1,
                          -1*z + c + q1 + q0 <= -2)),
                   And(q1 <= -1,
                       Or(z + -1*q1 + -1*q0 <= -1,
                          And(-1*z + c + q1 + q0 <= -1,
                              -1*c + z + -1*q1 + -1*q0 <= 0)))))))
    # cond = And(q0 >= 1,
    # Not(And(Not(0 >= -1*q0 + z), Not(c == -1 + -1*q0 + z))),
    # Not(And(q0 >= 1,
    #         Or(Not(q0 >= 1),
    #            z + -1*q0 <= -1,
    #            And(-1*z + c + q0 <= -1,
    #                -1*c + z + -1*q0 <= 0)))),
    # Not(And(-1*z + c <= -1,
    #         Or(Not(q0 >= 1),
    #            And(Not(0 >= -1*q0 + z),
    #                q0 + -1*z + c <= -1,
    #                Not(c == -1 + -1*q0 + z)),
    #            -1*q0 + z + -1*c <= 0))))
    nnf_cond = z3.Or([z3.And(*c) for c in z3.Tactic('nnf').apply(cond)])
    simplified = z3.simplify(converter.disjunction2z3(converter.to_dnf(nnf_cond))) 
    print(simplified)
    print(equals(cond, simplified))