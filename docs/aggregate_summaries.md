# Finite aggregate summaries

The scalar solver can retain finite sums and products for triangular affine
recurrences instead of failing when no elementary closed form is available.
The original multivariate pivot transformation is unchanged.

## Supported fragment

After substituting already solved scalar components, each remaining component
must have the form `x(n+1) = alpha(n)*x(n) + beta(n)`, with a supplied initial
value. Neither coefficient may depend on unresolved recurrence components.
Coefficients may contain immutable input-array functions. Branches can be
combined into piecewise coefficients when their guards no longer depend on
unresolved state. Nonlinear self-updates, cyclic component dependencies,
feedback-dependent guards, and reverse composition are not supported by this
fallback.

The solver uses the finite variation-of-constants identity: the homogeneous
part is the initial value times a prefix product; each forcing term is
multiplied by the product of subsequent coefficients and accumulated in a
finite sum. No coefficient is divided out, so zero coefficients are permitted.
The certificate kernel reconstructs this expression, checks branch coverage
and affine decomposition with SMT, and rejects unknown checks or a modified
candidate. This is a proof-schema certificate, not reliance on an SMT solver
to discover induction over uninterpreted aggregates.

## Representation and transport

Aggregates have half-open bounds `[lower, upper)`. Empty sums are zero and
empty products are one. The bound index is distinct from all explicit free
integer parameters. Definitions are alpha-normalized and content-named.
Nested aggregates are retained as a DAG. Sums/products over small constant
ranges can be expanded, with a 32-term per-interval limit and a shared budget.

Ordinary worker responses retain their SMT-LIB format. Aggregate responses use
the versioned JSON envelope `arith-exe-aggregates-v1`, containing the summary
SMT-LIB and a separate list of aggregate bindings. The native verifier stores
these definitions in the verification session, not as ordinary program
equalities. Definitions and caches are cleared between verification sessions.

## Verification limits

Small constant ranges are expanded after the loop iteration count and input
parameters have been substituted. Symbolic or large ranges remain functions
with exact aggregate metadata. The current native SMT encoding treats those
residual applications as an over-approximation. An UNSAT safety query is valid;
a SAT query that still contains an aggregate is UNKNOWN, not a counterexample.
For sums with bodies `r(i) + A(g(i))`, the verifier can also certify permutation
invariance on a symbolic finite integer interval. It compares two existing
sum applications, proves equal bounds and equal remainders pointwise, and
proves that both index maps are injective self-maps of that interval. A finite
injective self-map is bijective, so the array contributions have equal sums.
Identical index maps need only the pointwise check. Piecewise maps such as a
swap or rotation are supported; duplication, a shift outside the interval,
different remainders, and unequal intervals do not justify the identity.

These identities are query-local lemmas, not unconditional global axioms.
Their arithmetic assumptions are drawn only from conjuncts of the original
path condition, which remains in the SMT query unchanged. Every side condition
must be UNSAT-certified; unknown or timed-out checks add no lemma. The pass is
bounded to 16 sum applications, 24 checks of at most 50 ms each, and a one-second
deadline checked before each query. Products, multiple array reads per summand,
general aggregate inequalities, and automatic induction remain unsupported.

Loop-count synthesis first solves the independent guard constraints, before
conjoining unrelated quantified array assumptions. The independent candidate
must satisfy the guard constraints for every parameter valuation. All original
path constraints are retained in the instantiated loop summary.
Before composing the count into subsequent loops, it is restricted to the
actual entry path with the existing SMT-certified domain simplifier. For
example, `max(N, 0)` becomes `N` only when the path proves `N > 0`; without
that premise, the zero-trip region remains intact.

Composed array reads first use the quantifier-free arithmetic conjuncts of
their execution domain for simplification, then the full domain. The first
pass drops assumptions, rather than adding any, so its equality certificates
remain valid in the original context. This keeps unrelated array constraints
out of polynomial read certificates without discarding them from verification.

Set `ARITHEXE_ENABLE_AGGREGATES=0` to disable the scalar aggregate fallback.
Tests are in `test/tupling/test_aggregates.py` and the
`FiniteAggregateRepresentation`, `FiniteAggregateIntegration`, and
`FiniteAggregateIdentities` native suites.
