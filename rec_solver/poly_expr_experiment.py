#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time

import sympy as sp
from sympy.parsing.sympy_parser import (
    convert_xor,
    standard_transformations,
    parse_expr,
)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rec_solver.core.poly_expr import (  # noqa: E402
    find_cfinite_spaces,
    normalize_poly_expr_strategy,
    solve_cfinite_rec,
)


TRANSFORMATIONS = standard_transformations + (convert_xor,)


def parse_variables(raw_variables):
    if isinstance(raw_variables, list):
        names = raw_variables
    else:
        names = raw_variables.replace(",", " ").split()
    if not names:
        raise ValueError("At least one variable is required.")
    if len(set(names)) != len(names):
        raise ValueError("Variable names must be distinct.")
    return [sp.Symbol(name, integer=True) for name in names]


def default_initials(variables):
    return {var: sp.Symbol("%s0" % var, integer=True) for var in variables}


def parse_sympy_expr(raw_expr, local_dict):
    return sp.expand(parse_expr(
        str(raw_expr).replace("^", "**"),
        local_dict=local_dict,
        transformations=TRANSFORMATIONS,
    ))


def parse_assignment_items(raw_assignments):
    if isinstance(raw_assignments, dict):
        return list(raw_assignments.items())
    pieces = []
    for chunk in str(raw_assignments).replace("\n", ";").split(";"):
        chunk = chunk.strip()
        if chunk:
            pieces.append(chunk)
    items = []
    for piece in pieces:
        if ":=" in piece:
            lhs, rhs = piece.split(":=", 1)
        elif "=" in piece:
            lhs, rhs = piece.split("=", 1)
        else:
            raise ValueError("Expected assignment of the form x=expr: %s" % piece)
        items.append((lhs.strip(), rhs.strip()))
    return items


def parse_transition(raw_transition, variables, local_dict):
    transition = {var: var for var in variables}
    by_name = {str(var): var for var in variables}
    for lhs, rhs in parse_assignment_items(raw_transition):
        if lhs not in by_name:
            raise ValueError("Unknown transition variable: %s" % lhs)
        transition[by_name[lhs]] = parse_sympy_expr(rhs, local_dict)
    return transition


def parse_initial(raw_initial, variables, local_dict):
    initial = default_initials(variables)
    if raw_initial is None:
        return initial
    by_name = {str(var): var for var in variables}
    for lhs, rhs in parse_assignment_items(raw_initial):
        if lhs not in by_name:
            raise ValueError("Unknown initial variable: %s" % lhs)
        initial[by_name[lhs]] = parse_sympy_expr(rhs, local_dict)
    return initial


def load_problem(args):
    data = {}
    if args.input_json is not None:
        with open(args.input_json) as fp:
            data = json.load(fp)

    raw_variables = data.get("variables", args.variables)
    if raw_variables is None:
        raise ValueError("Provide variables with --vars or input JSON.")
    variables = parse_variables(raw_variables)
    local_dict = {str(var): var for var in variables}
    local_dict.update({
        str(value): value
        for value in default_initials(variables).values()
    })
    local_dict["n"] = sp.Symbol("n", integer=True)

    raw_transitions = data.get("transitions", args.transition)
    if raw_transitions is None:
        raise ValueError("Provide at least one --transition or input JSON transition.")
    if isinstance(raw_transitions, (str, dict)):
        raw_transitions = [raw_transitions]
    transitions = [
        parse_transition(raw_transition, variables, local_dict)
        for raw_transition in raw_transitions
    ]

    initial = parse_initial(data.get("initial", args.initial), variables, local_dict)
    strategy = data.get("strategy", args.strategy)
    degree = int(data.get("degree", args.degree))
    order = int(data.get("order", args.order))
    return variables, transitions, initial, normalize_poly_expr_strategy(strategy), degree, order


def characteristic_polynomial(coeffs):
    t = sp.Symbol("t")
    order = len(coeffs)
    poly = t ** order
    for i, coeff in enumerate(coeffs):
        poly -= coeff * (t ** (order - 1 - i))
    return sp.factor(poly)


def recurrence_text(coeffs):
    order = len(coeffs)
    terms = []
    for i, coeff in enumerate(coeffs):
        offset = order - 1 - i
        if sp.simplify(coeff) == 0:
            continue
        if offset == 0:
            term = "%s*q(k)" % coeff
        elif offset == 1:
            term = "%s*q(k+1)" % coeff
        else:
            term = "%s*q(k+%d)" % (coeff, offset)
        terms.append(term)
    rhs = " + ".join(terms) if terms else "0"
    return "q(k+%d) = %s" % (order, rhs)


def space_to_record(space, transitions, initial, variables, ind_var, include_closed_forms):
    record = {
        "recurrence_coefficients": [str(sp.simplify(c)) for c in space.recurrence_coeffs],
        "characteristic_polynomial": str(characteristic_polynomial(space.recurrence_coeffs)),
        "recurrence": recurrence_text(space.recurrence_coeffs),
        "basis": [str(sp.expand(expr)) for expr in space.basis_instances],
    }
    if include_closed_forms:
        closed_forms = []
        for expr in space.basis_instances:
            solved = solve_cfinite_rec(
                space.recurrence_coeffs,
                expr,
                transitions,
                initial,
                variables,
                ind_var,
            )
            if solved is None:
                closed_forms.append({"expression": str(expr), "closed_form": None})
            else:
                _, closed = solved
                closed_forms.append({
                    "expression": str(sp.expand(expr)),
                    "closed_form": str(sp.simplify(closed)),
                })
        record["closed_forms"] = closed_forms
    return record


def print_text(records, elapsed, variables, strategy, degree, order):
    print("variables: %s" % ", ".join(str(var) for var in variables))
    print("strategy: %s" % strategy)
    print("degree: %d" % degree)
    print("order: %d" % order)
    print("spaces: %d" % len(records))
    print("elapsed_seconds: %.6f" % elapsed)
    for idx, record in enumerate(records, 1):
        print()
        print("space %d" % idx)
        print("  recurrence: %s" % record["recurrence"])
        print("  characteristic: %s" % record["characteristic_polynomial"])
        print("  basis:")
        for basis_expr in record["basis"]:
            print("    - %s" % basis_expr)
        if "closed_forms" in record:
            print("  closed forms:")
            for closed in record["closed_forms"]:
                print("    - %s -> %s" % (closed["expression"], closed["closed_form"]))


def main():
    parser = argparse.ArgumentParser(
        description="Run polynomial-expression c-finite synthesis outside arith_exe."
    )
    parser.add_argument(
        "--input-json",
        help="JSON file with variables, transitions, initial, degree, order, and strategy.",
    )
    parser.add_argument(
        "--vars",
        dest="variables",
        help="Comma- or space-separated variables, e.g. x,y,z.",
    )
    parser.add_argument(
        "--transition",
        action="append",
        help=(
            "Branch transition, e.g. 'x=2*x; y=y+1'. "
            "Repeat for nondeterministic branches."
        ),
    )
    parser.add_argument(
        "--initial",
        help="Initial values, e.g. 'x=2; y=3'. Defaults to x0, y0, ...",
    )
    parser.add_argument("-d", "--degree", type=int, default=2)
    parser.add_argument("-r", "--order", type=int, default=1)
    parser.add_argument("--strategy", default="algorithm2", help="special, algorithm2, general, or auto.")
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of text.")
    parser.add_argument("--no-closed-forms", action="store_true", help="Do not attempt to compute closed forms.")
    args = parser.parse_args()

    variables, transitions, initial, strategy, degree, order = load_problem(args)
    if degree < 0:
        raise ValueError("degree must be nonnegative.")
    if order < 1:
        raise ValueError("order must be positive.")

    ind_var = sp.Symbol("n", integer=True)
    start = time.perf_counter()
    spaces = find_cfinite_spaces(
        variables,
        transitions,
        ind_var,
        degree,
        order,
        strategy,
    )
    elapsed = time.perf_counter() - start
    records = [
        space_to_record(
            space,
            transitions,
            initial,
            variables,
            ind_var,
            include_closed_forms=not args.no_closed_forms,
        )
        for space in spaces
    ]

    if args.json:
        print(json.dumps({
            "variables": [str(var) for var in variables],
            "strategy": strategy,
            "degree": degree,
            "order": order,
            "elapsed_seconds": elapsed,
            "spaces": records,
        }, indent=2))
    else:
        print_text(records, elapsed, variables, strategy, degree, order)


if __name__ == "__main__":
    main()
