"""Pure integer fold operators with bounded SMT certification of claimed laws."""

from dataclasses import dataclass
import json
import os

import z3


@dataclass(frozen=True)
class FoldOperator:
    name: str
    left: z3.ExprRef
    right: z3.ExprRef
    expression: z3.ExprRef
    laws: tuple

    def combine(self, left, right):
        return z3.simplify(z3.substitute(self.expression,
                                       (self.left, left), (self.right, right)))


def certify_operator(document, ctx):
    if document.get("sort") != "Int" or not isinstance(document.get("name"), str):
        raise ValueError("fold operators require a name and Int sort")
    text = document.get("combine")
    if not isinstance(text, str) or len(text) > 4096:
        raise ValueError("invalid fold combine expression")
    a, b, c = z3.Ints("a b c", ctx=ctx)
    result = z3.Int("__fold_result", ctx)
    parsed = z3.parse_smt2_string(f"(assert (= __fold_result {text}))",
                                decls={"a": a, "b": b, "__fold_result": result}, ctx=ctx)
    if len(parsed) != 1 or not z3.is_eq(parsed[0]):
        raise ValueError("combine must be a single expression")
    expression = parsed[0].arg(1)
    pending, seen = [expression], set()
    while pending:
        term = pending.pop()
        if term.get_id() in seen:
            continue
        seen.add(term.get_id())
        if len(seen) > 128 or not z3.is_app(term):
            raise ValueError("unsupported fold expression")
        if term.sort().kind() not in (z3.Z3_INT_SORT, z3.Z3_BOOL_SORT):
            raise ValueError("fold expressions must use integer/Boolean terms")
        if term.decl().kind() == z3.Z3_OP_UNINTERPRETED and not (z3.eq(term, a) or z3.eq(term, b)):
            raise ValueError("fold combine must be closed except for a and b")
        pending.extend(term.children())
    laws = tuple(document.get("laws", []))
    operator = FoldOperator(document["name"], a, b, expression, laws)
    combine = operator.combine
    obligations = {
        "associative": lambda: combine(combine(a, b), c) == combine(a, combine(b, c)),
        "commutative": lambda: combine(a, b) == combine(b, a),
        "idempotent": lambda: combine(a, a) == a,
    }
    for law in laws:
        if law not in obligations:
            raise ValueError(f"unsupported fold law: {law}")
        solver = z3.Solver(ctx=ctx)
        solver.set(timeout=200)
        solver.add(z3.Not(obligations[law]()))
        if solver.check() != z3.unsat:
            raise ValueError(f"uncertified fold law {law} for {operator.name}")
    if "identity" in document:
        identity = document["identity"]
        if not isinstance(identity, int) or isinstance(identity, bool):
            raise ValueError("fold identity must be an integer literal")
        solver = z3.Solver(ctx=ctx)
        solver.set(timeout=200)
        solver.add(z3.Or(combine(a, z3.IntVal(identity, ctx)) != a,
                         combine(z3.IntVal(identity, ctx), a) != a))
        if solver.check() != z3.unsat:
            raise ValueError("uncertified fold identity")
    return operator


def load_operators(ctx):
    path = os.environ.get("ARITHEXE_FOLD_OPERATORS")
    if not path:
        return ()
    with open(path, encoding="utf-8") as source:
        documents = json.load(source)
    if not isinstance(documents, list) or len(documents) > 16:
        raise ValueError("fold configuration must be a list of at most 16 operators")
    operators = tuple(certify_operator(document, ctx) for document in documents)
    if len({operator.name for operator in operators}) != len(operators):
        raise ValueError("duplicate fold operator name")
    return operators
