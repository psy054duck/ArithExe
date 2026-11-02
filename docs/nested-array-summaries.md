# Nested Array Summaries

In normal execution mode, loops with memory operations can use the recurrence
summarizer even when they have parents or children. While summarizing an outer
iteration, the executor summarizes each encountered inner loop, substitutes its
exit values and domain-restricted array solution, and then extracts the outer
recurrence. The additional recurrence iteration argument is instantiated with
the child's iteration count before composition.

The scalar nested-path accelerator remains available. Its unsupported
instruction profiles are cached per verification session, separately from
unsupported ordinary path-accelerator profiles. These caches contain structural
rejections only, not failed state-dependent proof obligations, and are cleared
when module state is cleared.

## Correctness Requirements

- Chained scalar GEPs in flattened VLAs accumulate offsets; typed fixed-array
  GEPs retain logical coordinates. Row offsets are not discarded.
- Array recurrence extraction recognizes identity frame expressions regardless
  of branch order, including frames produced by a substituted child summary.
- Child input-call counters start at the parent's current counters and advance
  when the child summary is applied. Incomplete counterexample reconstruction
  remains conservative.
- Assertions inside a child loop become guarded, quantified obligations for the
  outer recurrence. They are captured before completion assumptions, so a later
  abort cannot hide an earlier failing assertion.
- An incomplete or over-approximated child is not composed as an exact outer
  iteration. Unsupported nested-memory summaries return UNKNOWN instead of
  silently dropping the path or attempting enormous unrollings.
- The child iteration count must be a computed function of its entry state;
  a fresh count symbol constrained relationally is not treated as a constant
  across outer iterations.

## Solver Representation

`ArrayGuard.cpp` uses bounded ISL operations to propose simpler convex guards
from integer affine expressions, positive constant division/modulo, and shared
piecewise terms. Local graph variables are eliminated before exporting the
guard. Every replacement requires SMT equivalence to the original guard;
nonconvex guards, exhausted budgets, and unproved replacements are retained.

The scalar backend has a bounded integer-interval fallback for a single phase
length. Integer endpoints can contain floor division by a constant. A
quantifier-free implication check proves that the substitution preserves every
feasible parameter region.

The tupling reconstruction first proposes stopping times from the scalar
solution's existing phase boundaries. It proves nonnegativity, termination at
the proposed boundary, and absence of an earlier terminal state. The general
fallback also checks complete parameter coverage. Original recurrence-equation
certification is retained.

The original pivot implementation in `core/multivariate.py` is unchanged.
Integer-relaxed benchmark verdicts do not establish fixed-width C correctness.

## Guard-Boundary Phase Lengths

Before phase-template QE, unbounded plans can propose counts from affine guard
boundaries, including integer floor expressions. Bounded Boolean splitting
separates nonconvex entry and exit cases. Quantifier-free counterexample checks
prove every residue guard throughout each finite phase and the entire infinite
suffix. Counts from earlier phases are substituted into later phase entries.

The optimization shares the request's preprocessing budget and preserves the
existing constant-phase specialization. Certified proposals replace QE only
when they cover the current parameter region; otherwise the original template
solver runs unchanged. Inconclusive proofs do not establish coverage. Existing
scalar and original multivariate recurrence certificates are retained.

Reverse-composed return accumulators are supported only when guards and all
forward updates are independent of them. Every proposed boundary must have a
constant integer slope in the cycle counter; unsupported forms retain QE.

## Regression Tests

Native tests are in `NestedArrayRecurrence.*`, alongside the existing
`NestedPathAcceleration.*`, stack-array, array-composition, and input tests.
Python tests cover integer phase endpoints and complete stopping-time coverage.
Public SV-COMP sources are not modified for validation.
