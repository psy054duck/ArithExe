"""Exact bounded sums of piecewise integer-valued polynomials.

Boolean partitions are disjoint; affine comparisons clip half-open integer
intervals. Polynomial primitives are checked as rational polynomial identities,
and their integrality is certified in the multivariate Newton basis. No input
domain assumptions or floating-point arithmetic are used.
"""

import itertools
import math
import time

import sympy as sp
from sympy.functions.combinatorial.numbers import stirling
import z3


class Unsupported(ValueError):
    pass


def polynomial_sum(index, lower, upper, body):
    """Return an exact expression, or None when discovery exceeds its budget."""
    ctx = index.ctx
    deadline = time.monotonic() + .5
    remaining = [512]
    symbols, bindings = {}, {}
    variable = sp.Dummy('sum_index', integer=True)
    symbols[index] = variable
    bindings[variable] = index

    def iv(value):
        return z3.IntVal(value,ctx)

    def bv(value):
        return z3.BoolVal(value,ctx)

    def tick():
        remaining[0] -= 1
        if remaining[0] < 0 or time.monotonic() > deadline:
            raise Unsupported()

    def variables(expression):
        return sorted(expression.free_symbols, key=sp.default_sort_key)

    def integral(expression):
        """Integer Newton coefficients certify all integer parameter values."""
        tick()
        vs = variables(expression)
        if not vs:
            return expression.is_Integer is True
        poly = sp.Poly(expression, *vs)
        if poly.total_degree() > 6 or len(vs) > 6:
            return False
        coefficients = {}
        for powers, coefficient in poly.terms():
            for degrees in itertools.product(*(range(p+1) for p in powers)):
                tick()
                factor = math.prod(math.factorial(k)*stirling(p,k,kind=2)
                                   for p,k in zip(powers,degrees))
                coefficients[degrees] = coefficients.get(degrees,0) + coefficient*factor
        return all(c.is_Integer is True for c in map(sp.sympify,coefficients.values()))

    def convert(term):
        tick()
        if z3.eq(term,index):
            return variable
        if z3.is_int_value(term):
            return sp.Integer(term.as_long())
        if not z3.is_app(term) or not z3.is_int(term):
            raise Unsupported()
        kind = term.decl().kind()
        if kind == z3.Z3_OP_UNINTERPRETED and term.num_args() == 0:
            if term not in symbols:
                symbol = sp.Dummy('parameter',integer=True)
                symbols[term], bindings[symbol] = symbol, term
            return symbols[term]
        args = term.children()
        if kind == z3.Z3_OP_ADD:
            return sum(map(convert,args))
        if kind == z3.Z3_OP_SUB:
            return convert(args[0])-sum(map(convert,args[1:]))
        if kind == z3.Z3_OP_UMINUS:
            return -convert(args[0])
        if kind == z3.Z3_OP_MUL:
            return sp.prod(map(convert,args))
        if kind == z3.Z3_OP_IDIV and z3.is_int_value(args[1]) and args[1].as_long()>0:
            result = sp.expand(convert(args[0])/args[1].as_long())
            if integral(result):
                return result
        raise Unsupported()

    def encode(expression, replacements=None):
        tick()
        mapping = dict(bindings)
        mapping.update(replacements or {})
        numerator, denominator = sp.fraction(sp.together(expression))
        if not denominator.is_Integer or denominator <= 0:
            raise Unsupported()

        def integer(term):
            tick()
            if term.is_Integer:
                return iv(int(term))
            if term in mapping:
                return mapping[term]
            if term.is_Add:
                return sum((integer(v) for v in term.args),iv(0))
            if term.is_Mul:
                result = iv(1)
                for factor in term.args:
                    result = result*integer(factor)
                return result
            if term.is_Pow and term.exp.is_Integer and 0<=term.exp<=6:
                result = iv(1)
                for _ in range(int(term.exp)):
                    result = result*integer(term.base)
                return result
            raise Unsupported()

        result = integer(numerator)
        return result if denominator == 1 else result/iv(int(denominator))

    def primitive(term, lo, hi):
        polynomial = sp.Poly(convert(term),variable)
        if polynomial.degree() > 4:
            raise Unsupported()
        endpoint = sp.Dummy('endpoint',integer=True)
        prefix = sum(coefficient*(sp.bernoulli(power+1,endpoint)-sp.bernoulli(power+1,0))/(power+1)
                     for (power,),coefficient in polynomial.terms())
        prefix = sp.expand(prefix)
        # These exact algebraic checks certify the finite-sum identity for
        # every integer endpoint, including negative interval boundaries.
        if sp.expand(prefix.subs(endpoint,0)) != 0 or sp.expand(
                prefix.subs(endpoint,endpoint+1)-prefix-
                polynomial.as_expr().subs(variable,endpoint)) != 0 or not integral(prefix):
            raise Unsupported()
        value = encode(prefix,{endpoint:hi})-encode(prefix,{endpoint:lo})
        return z3.If(hi<=lo,iv(0),value)

    def minimum(a,b):
        return z3.If(a<=b,a,b)

    def maximum(a,b):
        return z3.If(a>=b,a,b)

    def depends(term):
        pending, seen = [term], set()
        while pending:
            child = pending.pop()
            if child.get_id() in seen:
                continue
            seen.add(child.get_id())
            tick()
            if z3.eq(child,index):
                return True
            pending.extend(child.children())
        return False

    def regions(guard,lo,hi,negative=False):
        tick()
        guard = z3.simplify(guard)
        if z3.is_not(guard):
            return regions(guard.arg(0),lo,hi,not negative)
        if z3.is_true(guard) or z3.is_false(guard):
            return [(lo,hi,bv(True))] if z3.is_true(guard) != negative else []
        if not depends(guard):
            return [(lo,hi,z3.Not(guard) if negative else guard)]
        if z3.is_and(guard) or z3.is_or(guard):
            conjunction = z3.is_and(guard) != negative
            if conjunction:
                result = [(lo,hi,bv(True))]
                for child in guard.children():
                    result = [(a,b,c & d) for l,h,c in result
                              for a,b,d in regions(child,l,h,negative)]
                    if len(result)>32:
                        raise Unsupported()
                return result
            # Disjoint Boolean union: A, !A & B, !A & !B & C, ...
            result, previous = [], bv(True)
            for child in guard.children():
                selected = z3.Not(child) if negative else child
                result.extend(regions(z3.And(previous,selected),lo,hi))
                previous = z3.And(previous,z3.Not(selected))
            return result
        kind = guard.decl().kind()
        if kind not in (z3.Z3_OP_EQ,z3.Z3_OP_DISTINCT,z3.Z3_OP_LE,z3.Z3_OP_LT,
                        z3.Z3_OP_GE,z3.Z3_OP_GT) or guard.num_args()!=2:
            raise Unsupported()
        difference = sp.Poly(convert(guard.arg(0))-convert(guard.arg(1)),variable)
        if difference.degree()!=1 or difference.nth(1) not in (1,-1):
            raise Unsupported()
        sign = difference.nth(1)
        bound = encode(-difference.nth(0)/sign)
        relation = {z3.Z3_OP_EQ:'eq',z3.Z3_OP_DISTINCT:'ne',z3.Z3_OP_LE:'le',
                    z3.Z3_OP_LT:'lt',z3.Z3_OP_GE:'ge',z3.Z3_OP_GT:'gt'}[kind]
        if sign == -1:
            relation = dict(le='ge',lt='gt',ge='le',gt='lt',eq='eq',ne='ne')[relation]
        if negative:
            relation = dict(le='gt',lt='ge',ge='lt',gt='le',eq='ne',ne='eq')[relation]
        yes = bv(True)
        if relation == 'lt': return [(lo,minimum(hi,bound),yes)]
        if relation == 'le': return [(lo,minimum(hi,bound+1),yes)]
        if relation == 'ge': return [(maximum(lo,bound),hi,yes)]
        if relation == 'gt': return [(maximum(lo,bound+1),hi,yes)]
        if relation == 'eq': return [(maximum(lo,bound),minimum(hi,bound+1),yes)]
        return [(lo,minimum(hi,bound),yes),(maximum(lo,bound+1),hi,yes)]

    def total(term,lo,hi):
        tick()
        pending, seen = [term], set()
        while pending:
            node = pending.pop()
            if node.get_id() in seen:
                continue
            seen.add(node.get_id())
            tick()
            if z3.is_app(node) and node.decl().kind() == z3.Z3_OP_ITE:
                result = iv(0)
                for choice in (True,False):
                    branch = z3.substitute(term,(node,node.arg(1 if choice else 2)))
                    for a,b,condition in regions(node.arg(0),lo,hi,not choice):
                        result += z3.If(condition,total(branch,a,b),iv(0))
                return result
            pending.extend(node.children())
        return primitive(term,lo,hi)

    try:
        return z3.simplify(total(body,lower,upper))
    except (Unsupported,sp.PolynomialError,ValueError,TypeError):
        return None
