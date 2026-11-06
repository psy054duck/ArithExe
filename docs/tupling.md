# Tupling backend

The original pivot implementation in
`rec_solver/rec_solver/core/multivariate.py` is unchanged. The default strategy
is still `pivot`, including its existing bounded-univariate fallback. Tupling
is opt-in and never silently falls back to pivot or bounded enumeration.

## Algorithm and attribution

The new backend instantiates the established definition/unfolding/folding
tupling strategy for guarded integer recurrences. It is not ECCE, not a full
implementation of the MRP structural analysis, and not a claim of a new general
tupling algorithm. Its numerical extensions are affine argument normalization
and SMT-checked matching under arithmetic path conditions.

References:

- De Schreye, Glueck, Joergensen, Leuschel, Martens, and Soerensen.
  *Conjunctive Partial Deduction: Foundations, Control, Algorithms, and
  Experiments*. Journal of Logic Programming 41(2-3):231-277, 1999.
- Chin, Khoo, and Jones. *Redundant Call Elimination via Tupling*.
  Fundamenta Informaticae 69(1-2):1-37, 2006, especially Figure 5.
  https://www.comp.nus.edu.sg/~khoosc/papers/Fundamenta_05.pdf
- ECCE source and license: https://github.com/leuschel/ecce

Each tuple component has an explicit meaning `f(T_i(x))`, with the first
component equal to the original `f(x)`. Starting with that component, the
strategy unfolds the selected first computation, splits feasible source cases,
evaluates provable base calls, and retains other tuple members for folding.
For an affine child-state map `g`, a leaf `f(h(x))` folds to component `i` only
when the current guard entails `h(x) = T_i(g(x))`. Unmatched leaves introduce
the component `f(h(g^-1(x)))`, and specialization restarts. This is selective
unfolding: unfolding every Fibonacci component at once would grow the tuple
without reaching the usual two-component solution.

Acceptance requires exhaustive disjoint source cases, a finite tuple within the
configured bounds, exact guarded fold identities for every result component,
constant integer coefficients of recursive results, at most one distinct
recursive tuple call per residual branch, and one common integer ranking that
is nonnegative and strictly decreases on every recursive branch. Supported
ranking templates are arguments, their negatives, and argument differences.
The ranking proves termination of the residual computation. Exact unfold/fold
identities then establish its component interpretation by induction. These
checks concern the transformation. Scalar closed-form solving reuses the
existing downstream solver, but its output is treated as a candidate: the new
route additionally checks every original base and recursive equation by SMT
after substituting that candidate. Unresolved functions, failed equations, and
SMT `unknown` are rejected. The original pivot solver is not changed by these
additional checks.

Before tuple construction, the solving entry point checks
`MultiRecurrence.is_nearly_tail()`. If each branch already has at most one
distinct recursive call, it invokes `solve_nearly_tail` on the original
recurrence without selecting a fold anchor or computing an inverse. Source
admissibility, decreasing-rank checks, and final closed-form certification
remain enabled. Before scalar conversion, branches proved infeasible by SMT
are removed from the working recurrence; the final certificate still checks
the original recurrence. An SMT-unknown result rejects the attempt rather
than dropping a possibly reachable branch. This prevents obsolete array-read
functions in impossible branches from being misidentified as another
induction variable. Only inputs with multiple recursive calls enter tuple
construction. This is a structural fast path, not a fallback to the pivot
transformation, and it does not integrate ECCE into the verifier.

The LLVM loop summarizer retains an array's entry summary when all final
states have structurally identical values and definedness. This avoids solving
identity recurrences for read-only arrays and preserves their existing domains.
Assertions recorded by the instruction interface are passed to the legacy
summarizer as guarded obligations, including their definedness and the loop
entry constraints. The C++ `ArraySummaryComposition` regressions check both
a correct polynomial assertion (including a conditional assertion) and an
incorrect assertion on two sequential array loops.

Array-domain restriction now simplifies nested conditionals using bounded Z3
entailment checks instead of eagerly expanding them through quantifier
elimination. Rewrites are justified only under the supplied domain; unknown
queries or an exhausted query budget retain the unresolved expression. The
logic helpers handle non-application AST nodes, and CNF conversion retains
the original formula if a new implicant cannot be certified.
Before extracting array recurrences, update expressions are simplified under
the full execution path, including the loop guard. This eliminates old-array
fallbacks outside the readable domain without assuming that a summary holds
for arbitrary array indices. Read-only loads receive the same contextual
simplification, including when LLVM alias analysis permits direct reuse.

Targeted Ubuntu validation of this composition path proves the sequential-array
safe regression and rejects its incorrect polynomial assertion. The official
SV-COMP `array-fpi/eqn1f.c` task returns `FALSE(unreach-call)` (its expected
verdict) in approximately 37 seconds with the tupling strategy and default
ILP32 semantics. This is a single-task result, not a rerun of all array-fpi.

`test/tupling/eqn1f_single_call.rec` captures the singular-call regression:
under `i == n`, `array(i,n)` calls `array(n-1,n-1)`. Its dispatch test supplies
a known exact candidate to check the routing and original-equation certificate
independently of scalar-solver support; that test alone is not an end-to-end
verification of `eqn1f.c`. A separate end-to-end test covers a nearly-tail recurrence
with an argument reset to zero, whose argument map is also noninvertible.

SMT `unknown`, missing coverage, nonlinear result combinations, non-affine
arguments, non-unimodular fold anchors, failed ranking search, and resource
limits raise `TuplingUnsupported`. Bounds do not justify an approximate summary.
The input recurrence is not mutated.

## Using it

From the repository root, with the recurrence solver's Python dependencies:

```sh
python tools/tuple_recurrence.py test/tupling/fibonacci.rec
python tools/tuple_recurrence.py test/tupling/guarded_affine.rec
python -m unittest discover -s test/tupling -v
```

The inspection command returns JSON with component interpretations, guards,
the single recursive call, result expressions, ranking, and SMT query count.
It does not claim to have solved the residual scalar recurrence.

Python API:

```python
from rec_solver import solve_file
closed = solve_file("test/tupling/triangular.rec",
                    multivariate_strategy="tupling")
```

For the C++ verifier/solver worker:

```sh
ARITHEXE_MULTIVARIATE_STRATEGY=tupling ./build/arith_exe program.c
```

Set `ARITHEXE_SOLVER_PYTHON` to the intended Python executable when necessary.
Rerun CMake after Python changes because the existing build copies these files
at configuration time. LoopRecurrence dispatch is unchanged by this option.
Use `pivot` or unset the option to run the original implementation.

### Integer-model experiments

For the current mathematical-integer experiments, select the existing
IR-level relaxation explicitly:

```sh
python tools/submit_benchexec.py --semantics integer \
  --output-dir /path/to/results/array-fpi-integer --workers 2
```

This passes `--ignore-bitwidth-constraints` to the verifier. Verdicts remain
marked `integer-relaxed` in raw logs. These experiments do not establish
fixed-width C correctness. LLVM IR and the ILP32/LP64 memory model are still
used; the integer relaxation is not a new source-language frontend.
Arithmetic semantics is independent of acceleration selection: integer mode
still uses recurrence summaries for ordinary loops. Set
`ARITHEXE_FORCE_PATH_EXPRESSIONS=1` explicitly for path-expression experiments.
Integer mode does not reuse LLVM's fixed-width dependence results to justify
array snapshot reads. The ISL analysis described below proves entry reads;
unproved reads retain their current-loop-state interpretation.
Some dependent-accumulator summaries remain imprecise and return
UNKNOWN rather than an unproved safe verdict or a spurious counterexample.
Nondeterministic inputs retain their declared type domains; arithmetic and
casts use the existing integer relaxation within those input domains.

The runner generates separate YAML task definitions without expected
verdicts, so BenchExec reports unscored true/false outcomes rather than
classifying them against fixed-width SV-COMP labels. Original tasks and
labels remain in the provenance manifest. The default `--semantics c`
retains the existing fixed-width experiment behavior.

### Verification calls and certified iteration counts

Loop execution does not model recognized assertions, assumptions, or error
calls as nondeterministic return values. They retain the instruction engine's
semantics, including declared `__VERIFIER_assert`, `__VERIFIER_assume`,
`__VERIFIER_error`, `reach_error`, and `__assert_fail`. A feasible direct error
encountered by the one-iteration explorer invalidates that loop summary.
Ordinary execution then handles the error; a summary of only the normal
completion paths is not accepted. Unproved nested summaries remain UNKNOWN.

Iteration-count solving first checks whether a model-proposed numeral is
forced by the full constraints, with a bounded SMT counterexample query.
Nonconstant systems are eliminated over rationals only when their unknown
coefficients are numeric. Integer candidates are checked for uniqueness
against the original equations and the combined DNF result is certified
against the complete input constraints. Symbolic pivots, singular systems, failed
exact divisions, and inconclusive checks do not justify a solution. The
count constraints are retained after substitution of a solved count.

### Polyhedral entry-array reads and symbolic base values

For input-dependent loops, identical scalar updates are collapsed only after
bounded SMT proves their guards exhaustive; an implicit identity update is
retained when a path does not update the state. This avoids polynomial-space
search for counters whose updates do not depend on the input branches.

External-input normalization merges base cases with identical return values.
It simplifies irrelevant tests and uses affine write-index equalities to
rewrite accesses, including strided writes: under `k == 2*t-2`,
`input(2*t-2)` becomes `input(k)`. Rational intermediate substitutions are
allowed only when the final access arguments are integer affine expressions.
Each guard rewrite is checked for Boolean equivalence and each value rewrite
for equality under its branch guard. Distinct neighboring inputs remain
distinct, and accesses that still vary during recursion are rejected. The
original recurrence is retained for final closed-form certification.

The normal tupling path no longer guesses a single-write interval or a prefix
write diagonal. `prefix_scan.py` remains an experimental standalone helper,
but neither it nor a pointwise candidate generator is called by the solver.

`lib/EntryArray.cpp` uses ISL integer-set emptiness to prove that no write in
the analyzed loop can precede and overlap a read. It extracts constant-step
header induction variables and affine GEP byte offsets. Write/read times
range over all nonnegative integers; data-dependent guards and upper bounds
are conservatively omitted. Same-iteration order is determined by CFG
reachability without crossing the loop header, and overlapping byte ranges,
not just equal element indices, are checked. The analysis currently supports
acyclic iterations of innermost loops in integer-relaxed mode. Unsupported
indices, aliases, volatile/atomic accesses, or memory-writing calls prevent
the snapshot optimization. Possible dependence or an ISL failure never
justifies a snapshot. ISL is a required C++ dependency (`libisl-dev` on Ubuntu).

Proved reads use the loop-entry array value. `core/base_inputs.py` validates
integer-affine arguments of external applications and uses entailed branch
equalities to normalize their indices. Only applications proved invariant
along every recursive edge can become free scalar parameters in guards or
recursive updates. Changing input applications in base returns remain
symbolic and are evaluated at the terminal argument state by the existing
APS solver. Unknown SMT checks cannot establish these transformations.

The ordinary `solve_nearly_tail` path solves the scalar recurrences and
stopping time. Parameters are restored to their original input applications,
and the result must satisfy every original base and recursive equation.
No event time or array closed form is guessed. This representation extension
does not guarantee successful tupling or make varying input-dependent
traversals solvable. Original pivot code remains unchanged.

The tupling backend uses `core/dag_reconstruction.py` for result reconstruction.
It reuses the existing APS conversion, conditional scalar solver, and
stopping-time computation without changing those algorithms. The solved state
is exported once; terminal arguments are substituted directly into base
expressions, and base alternatives remain shared Z3 `If` expressions instead
of being expanded into cases and merged again. The restored result is still
certified against the original recurrence. The legacy pivot backend retains
its original reconstruction path. Dedicated tests check equivalence with the
legacy path, vector return substitutions, piecewise bases, and DAG size.

Tests: `test_base_inputs.py`, `test_pointwise.py` (now requiring scalar
dispatch), and the native `PolyhedralEntryReads.*` suite. Historical timings
from the removed shortcuts should not be presented as results of this path.

## Reusing ECCE first

The existing Linux executable distributed in ECCE's `bin/Linux/ecce.tgz` runs
on the Ubuntu test server as ECCE 2.0. The current source build requires
SICStus Prolog; that runtime is not installed there. The archived binary is
tested separately, rather than presented as the engine of the Python backend.

Reproduce a baseline run without changing ECCE:

```sh
python tools/run_ecce_tupling.py test/tupling/integer_fibonacci.pl \
  --ecce /path/to/linux/ecce --output-dir /path/to/results/ecce-integer
python tools/run_ecce_tupling.py test/tupling/peano_fibonacci.pl \
  --ecce /path/to/linux/ecce --msv --output-dir /path/to/results/ecce-peano
```

The runner records executable/source hashes, command, output, and residual
Prolog. Initial direct integer Fibonacci tests using the default and
`deforest` configurations retained two recursive calls. This is an observed
limitation of those encodings/configurations, not a proof that all ECCE
configurations or all tupling algorithms fail. The integer extension can
recognize the affine shifts and derive `(f(n), f(n-1))` directly.

## Constant-modulo normalization

The scalar recurrence solver splits constant-divisor integer modulo in update
expressions into residue-guarded branches before choosing a solving backend.
For example, `s(n+1) = s(n) + i(n)%3` becomes three branches guarded by
`i(n)%3 == r`, with update `s(n+1) = s(n) + r`, for `r = 0,1,2`.
This is solver preprocessing, not an LLVM extraction rule, and also applies
to scalar recurrences generated internally by APS conversion. The original
pivot transformation module is unchanged.

Repeated terms share a residue across all simultaneous updates; nested terms
are processed inside out. The original guards remain in force. Only branches
proved infeasible are pruned; SMT `unknown` retains the branch. Initial values
and modulo appearing only in guards are not expanded. Nonconstant and zero
divisors are left to the existing backends. The transformation uses Z3 integer
modulo semantics, not C's signed remainder; frontend translation remains
responsible for source-language semantics.

`solve_str` and `solve_file` accept `modulo_max_branches` (default 128).
The worker exposes it as `ARITHEXE_MODULO_MAX_BRANCHES`. The limit is scoped
per solve and inherited by internal scalar calls; zero disables splitting.
If the conservative expansion bound exceeds the limit, preprocessing logs a
warning and retains the entire original recurrence. It never drops feasible
residues to fit the limit, and the existing solver may still report unsupported
or return a partial solution. This is not a new closed-form shortcut for the
`ms` benchmarks.

`test_modulo_normalization.py` checks one-step equivalence over arbitrary
integer states, shared and nested residues, negative operands, infeasibility,
SMT `unknown`, metadata preservation, scoped limits, and periodic sums for
moduli 2--5. These tests also cover a correction to periodic suffix rotation:
rotating a sampled cycle must preserve all of its entries.

## Sample-guided phase lengths

Before building general quantified phase constraints, the scalar solver uses
each sampled finite phase `A^c` as a proposed upper bound. It unrolls `c+1`
executions of the entire component, including every branch guard, from an
arbitrary phase-entry state. Only SMT UNSAT proves an upper bound. Initial
state assumptions are not silently reused at later phase entries; proofs may
use parameter-only preconditions. Reverse-composed APS states conservatively
retain the general path. The final infinite suffix is never bounded using its
finite sampled witness.

A proved bound of one fixes a nonempty phase count to one. Larger small
bounds enumerate all counts from one to the bound, not just the sampled
count. If enumeration would exceed its limit, the count remains symbolic with
the proved upper bound retained. SAT, UNKNOWN, exhausted time, and large
unrollings leave the original symbolic count unresolved.

The default budgets are 100ms per SMT check, 32 primitive transitions per
phase including the attempted extra execution, enumeration up to eight, at
most 16 combined template variants, and one cumulative second of preprocessing
per solve request. The cooperative time budget is shared across sampled
templates, parameter regions, and nested scalar calls. Checks are clamped to
the remaining budget; proof caching avoids repeated work. Python/Z3 expression
construction is not asynchronously interrupted. QE and candidate certification
are measured separately rather than charged against this preprocessing budget.

Fixed finite phases are instantiated directly. Remaining quantified phases
use one relative cycle counter, substituting `start + period*k + residue`
into each guard. This removes absolute-index congruence premises and avoids
one quantified variable per residue. If no symbolic count remains, the
constraint itself is the parameter region; no empty-vector arithmetic synthesis
is invoked. Redundant second QE passes over already eliminated variables are
omitted. Scalar candidates from the enabled symbolic path are checked against
the input recurrence, including initial values. Inconclusive checks cannot
accept a specialized candidate; any general fallback is checked too.

`solve_str`/`solve_file` accept `phase_length_options` (a dictionary or
`PhaseLengthOptions`) and an optional `phase_length_stats` output dictionary.
The options are `enabled`, `check_timeout_ms`, `max_transitions`,
`max_enum_count`, `max_variants`, and `total_time_ms`. The corresponding worker
environment settings are `ARITHEXE_PHASE_PREPROCESSING`,
`ARITHEXE_PHASE_CHECK_TIMEOUT_MS`, `ARITHEXE_PHASE_MAX_TRANSITIONS`,
`ARITHEXE_PHASE_MAX_ENUM_COUNT`, `ARITHEXE_PHASE_MAX_VARIANTS`, and
`ARITHEXE_PHASE_BUDGET_MS`. `ARITHEXE_PHASE_STATS=1` emits per-request JSON
statistics to stderr without changing the framed solver protocol. Disabling
the bound preprocessing leaves counts symbolic; the relative-cycle constraint
encoding remains in use.

Tests in `test_phase_lengths.py` cover sound bounds, nonconstant bounded
counts, whole-component and simultaneous-update semantics, time-dependent
guards, budgets, unknown checks, caching, enumeration limits, scoped telemetry,
parameter-name collisions, finite instantiation, relative-cycle constraints,
and rejection of uncertified candidates including the general fallback.
Original pivot transformation source remains unchanged.

## Current limits

This is a bounded numerical tupling backend, not a complete algorithm for
arbitrary recursion. It currently supports one scalar-valued function with
distinct integer formal arguments, exclusive exhaustive guards, and affine
call arguments. Mutually recursive and nested-call functions are unsupported.
Array indices can be symbolic parameters: `multidimensional.rec` tuples a
three-argument array recurrence while preserving both index arguments.

Moving-write array recurrences can require frontier components in addition to
the queried cell. The bounded search first tries selective unfolding, then
unfolds auxiliary components as well. Singular leaf calls can be lifted to
invertible child-state maps by preserving formal coordinates; affine equalities
proved under the branch guard also propose coordinate rewrites. These maps are
discovery candidates, not assumptions: every leaf must match a tuple component,
every fold identity must be certified, and one common rank must decrease.
Component, branch, and round limits still apply. The array Fibonacci fixture
remains a rejection test at a three-component budget; transformation success
does not establish that its downstream closed form can be computed.

For genuinely tupled array histories, a bounded polynomial-candidate pass can
avoid an expensive APS traversal. It discovers a possible written-cell domain
by Presburger projection of update guards and uses the existing polynomial
template generator (degree at most two, at most sixteen monomials). Integer
coefficients are refined by SMT counterexamples, with a two-second search
budget. No sampled fit is accepted without universal checks of every tuple
equation, followed by the original recurrence certificate. Unknown results or
budget exhaustion leave the existing APS path intact. The write-domain shape
is only a template proposal; neither unique writes nor the proposed domain is
trusted as an independent acceleration theorem.

The frontend exposes established entry-memory expressions in base cases only
for multi-read recurrences using the tupling strategy. This preserves initialized
seed cells that would otherwise be hidden behind an unconstrained input
function until after solving. It does not infer seeds from assertions, and the
original pivot route is unchanged. `multipath_array_progression.rec` and the
positive/negative multipath progression C fixtures exercise this path.
Entry-memory conditionals involving a changing formal coordinate are not
abstracted into invariant scalar parameters; stable-coordinate abstraction is
retained. A direct branch to a pure verifier-error block is recorded as an
assertion obligation, including at a latch, rather than assumed on the normal
body path. Only normal exits select the continuation, and an unhandled error
exit still rejects summarization. A declared nonreturning `abort` terminates
ordinary execution; inside summarization it remains unsupported rather than
being mistaken for a completed iteration.
No whole-SV-COMP performance improvement is claimed from the unit tests.

Initial downstream-solver check: `guarded_affine.rec` is successfully tupled,
but the existing scalar solver's candidate fails the original equations and
is rejected by `solve_tupled_rec`. Transformation success and closed-form
success must therefore be reported separately. `triangular.rec` passes both
stages and the framed solver-worker protocol test.
`arithmetic_progression.rec` exercises both multiple-call elimination and
scalar solving: `2*f(n-1,a)-f(n-2,a)` tuples to two components and yields the
certified closed form `a+n` over the entire integer domain of its definition.

Ubuntu smoke validation also proves `test/tupling/true_triangular.c` safe with
the new backend, using the ordinary C frontend and solver worker. The initial
20-second array smoke runs time out with both routes; those timeouts are not
evidence of a tupling-versus-pivot separation.

`test/tupling/true_tupled_linear.c` is also proved safe through a successfully
solved recurrence with `ARITHEXE_SOLVER_TIMEOUT_MS=60000`; the initial
10-second worker limit was too small. It checks `progression(n,a)=a+n` for
`0<=n<=50` and `0<=a<=100` under the ordinary ILP32 verifier semantics.
The Python tests include nearly-tail dispatch regressions, exact tuple-component comparisons,
multi-path and cross-argument cases, a three-call recurrence, scalar
certification, framed worker integration, and explicit rejection paths.
