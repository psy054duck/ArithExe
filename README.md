# ArithExec

## Note

When compile in Release mode, this project behave weird.
This may be caused by bugs in z3.
For now, please compile with debug mode by
` cmake -DCMAKE_BUILD_TYPE=Debug ..`

## Introduction
ArithExe is a integer program verifier based on symbolic execution (SE).
It well known that SE suffers path explosion due to loops and recursion.
To address this problem, a recurrence solver based on our paper [1, 2]
are integrated.

## Dependencies
This project are written in both C++ and Python.
Libraries and packages required by them are listed below.

### C++ Libraries
Version numbers indicate the version we use during the development.
Other versions may also applicable.

* Boost
* LLVM 20.1.8 (the current frontend uses LLVM's debug-variable records)
* Z3 4.13.4.0
* google test (can be download automatically by provided CMakeLists.txt)
*  spdlog (also download by cmake automatically)

### Python Libraries
See `requirements.txt`


## Build
1. Make all dependencies ready.
    * Python environment can be built by installing 'requirements.txt'
    * It is required to install google test manually. It will be download by `cmake`.

2. build using `cmake`. In the project root, run the following commands:
```
mkdir build
cd build
cmake ..
make
```
it may take tens of seconds to compile the whole project.

3. Check if build successfully by running google test
This project uses google test as unit test framework.
There have been some tests included in this repo.
Run the following commands to see them.
```
cd test
ctest
```

## Usage
After building, an executable called `arith_exe` will be in `build` folder.
The following is the format to run it. A conclusive result writes an
SV-COMP YAML 2.1 witness to `witness.yml` by default.
```
./arith_exe filename.c
```

Pass `--verbose` (or `-v`) to display symbolic-execution progress and the
intermediate path-expression pipeline: observed loop paths, compressed
prefixes, candidate schemas, composite guards and updates, certified closed
forms, first-break constraints, inferred repetition counts, and residual-state
feasibility.

```
./arith_exe --verbose filename.c
```

For an SV-COMP run, pass the task's property file and data model explicitly:

```
./arith_exe --property-file unreach-call.prp --data-model=ILP32 \
  --witness witness.yml filename.c
```

Witness generation currently supports the unreach-call property used by the
ReachSafety Loops and Recursive categories.

`--no-witness` disables witness output. Correctness witnesses contain an
unreachability invariant at every source-level error call. When ArithExe finds
an exact closed form for a loop, it also exports that result as a loop invariant
with YAML 2.1 ghost instrumentation. A ghost iteration counter is initialized
before the loop and incremented after each loop-body execution. If a closed
form depends on a variable's value on entry, a second ghost variable snapshots
that value before the loop; these ghost identifiers intentionally do not occur
in the input program. Exact scalar summaries of recursive functions are
exported as YAML 2.1 function contracts whose postconditions relate `\result`
to the pre-state formal parameters through `\at(parameter, Old)`. Violation witnesses
contain concrete `function_return` constraints for the executed
`__VERIFIER_nondet_*` calls, in execution order, followed by a target waypoint
at the feasible error location. For summarized loops, calls that execute once
per iteration are expanded using the model's concrete iteration count. Both
witness kinds include the input SHA-256, specification, architecture, producer,
timestamp, and UUID required by the exchange format.

If a summarized loop or recursive function hides a conditional nondeterministic
call whose dynamic return sequence cannot be reconstructed, ArithExe reports
`UNKNOWN` instead of emitting a potentially non-reproducible violation witness.

## Trace-driven path-expression acceleration

ArithExe also contains an exact fallback accelerator for multi-path loops.  When
the ordinary whole-loop recurrence summarizer is unavailable, every symbolic
state records the precise conditional branch and `select` decisions made by one
header-to-header iteration.  At every completed iteration it:

1. interns the iteration as a path symbol;
2. runs deterministic minimum-cost primitive-power compression on the exact
   prefix;
3. treats the final primitive word as a starred candidate, never as a proof;
4. composes the guarded maps of the candidate's constituent paths;
5. solves and checks a closed form for that composite map; and
6. jumps to its exact first break while retaining the complementary ordinary
   symbolic state.

The accelerated state carries the exact relation

```
k >= 1
and forall t. 0 <= t < k => guard_w(F_w^t(state))
and not guard_w(F_w^k(state))
```

and records `w^k` in its segmented exact path prefix. Quantifier elimination is used to
recognize a functional affine exponent when Z3 exposes one; otherwise `k`
remains a constrained relational template.  Unsupported loops, failed
closed-form certificates, solver `unknown`, or unsuccessful first-break queries
retain the ordinary symbolic state. The independent coarse proof described
below is attempted at loop entry, without these exact-path constraints.

Returning-path enumeration executes the latch branch before interning the
iteration. Conditional backedge decisions therefore agree with the observed
prefix, and the returning guard includes the latch condition. A latch exit
is not treated as another repetition: the accelerator stops at the first
failure of the returning guard and resumes ordinary execution at the header,
which processes the actual exit and its live-out PHIs. This also handles
scalar loops with an internal `break`, such as `loop-zilu/benchmark09_conjunctive`.

The trace-driven certified implementation profile is deliberately conservative:
at least one integer scalar PHI, side-effect-free loop bodies, no nested loop, and a
solvable composite path map.  Most calls, loads, stores, and nested loops fall back
to the existing engine.  Translation maps use a built-in closed form; other
polynomial composites are sent to the existing recurrence solver and accepted
only after base and step checks are proved unsatisfiable.

### Bottom-up nested-loop acceleration

Nested loop families additionally use a certified, parametric exit-summary
route at entry. It explores one parent iteration with each child replaced by
its certified complete exit, building summaries from the innermost loop outward.
This currently supports deterministic scalar counted loops with one returning
body path, a header exit, integer PHIs, and a monotone `<`, `<=`, `>`, or `>=`
counter test with a nonzero constant stride. Translations and reset maps have
built-in closed forms; other maps use the recurrence solver and base/step checks.

For a synthesized functional count `N`, acceptance requires the current entry
constraints to entail the exact summary domain:

```
N >= 0
and exit_guard(F^N(inputs))
and forall t. 0 <= t < N => returning_guard(F^t(inputs))
and forall t. 0 <= t <= N => not error_guard(F^t(inputs))
```

Assertion failures are collected as error obligations, never assumed away.
Child domains must be proved at their invocation; an unsupported or uncertified
child prevents its parent from being summarized. External scalar live-ins are
explicit parameters of the per-session summary cache, so a summary cannot be
specialized to an earlier outer iteration. Zero-trip exits preserve incoming
live-outs, including variables reset only by a nonempty body. Each dynamic
invocation has a fresh path-prefix cursor.

Piecewise child exits are encoded as explicit guarded branches, not `ite`
updates. For example, a child incrementing `k` for `j = 0; j < m; ++j`
has the exits `m <= 0: k' = k` and `m > 0: k' = k + m`.
Counts and live-outs are split together to preserve correlations. A parent
composes each branch separately into a recurrence; construction does not use
the caller's path condition to specialize the cached summary. Acceptance
checks coverage by the union of certified branch domains. Case sets exceeding
64 branches or lacking certified coverage fall back without accepting a
partial summary. Returning alternatives introduced by child summaries are
supported; arbitrary switching between native body paths remains unsupported.

The route works in both default fixed-width mode and the experimental integer
relaxation. It retains definedness and fixed-width guard constraints in default
mode. Domain QE is optional and limited to 100 ms; entry certificates are checked
with the exact domain when QE does not finish. Multiple returning body paths,
memory effects, fresh body calls, unsupported counter templates, and solver
`unknown` fall back to ordinary execution. For nested families this fallback
does not invoke the legacy nested recurrence route. Verbose mode logs construction,
counter templates, safety/coverage checks, and jumps under `[nested-path]`.

For example, `test/benchmark/path_expression/true_nested5_1.c` builds five
summaries and skips all 268,435,455 outer repetitions, including the assertions
inside the `z` loop. No increase in the trace-compression root bound is needed.

Direct external Boolean and standard integer nondeterministic header guards
support arbitrary finite repetition. A no-argument scalar call must feed only
one branch in the same block: directly for a Boolean, or through `== 0` /
`!= 0` for an integer, with optional single-use Boolean negation. Reversed
zero comparisons are accepted. Values also used as data, arbitrary external
functions, and other predicates remain outside this profile. The header must
be the only loop exit and contain only PHIs, the call, its control predicates,
and the branch (apart from debug records). The coarse union-star summarizer
also admits these control-only calls in the body, including integer truthiness
used as the condition of one or more same-block LLVM `select` instructions.
All uses must be select conditions, and selections sharing the predicate retain
one correlated fresh choice (as in `bhmr2007`), not independent choices.
Such a select is not
accepted as a header exit, and oracle values also used as data are still rejected
by the polynomial route. This covers the unsigned
header/body oracles in `eq1` and preserves `w == x` and `y == z`.
The accelerator currently accepts one repeated path symbol and proves that
no competing body path can be enabled anywhere on its closed-form trajectory
under the current path condition. This handles invariant choices such as
`if (flag)`; a changing body path falls back to ordinary execution until a
stable path can be certified.

Each dynamic guard call is an independent Boolean oracle value `b(t)`.
Existentially projecting a sequence of `n` continue values followed by an exit
value permits every `n >= 0`. The accelerated exit retains

```
n >= 0 and forall t. 0 <= t < n => guard_w(F_w^t(state))
```

including the definedness conditions of all scalar updates. A sort-correct
sequence is retained for counterexamples: a lambda array containing `n`
continue values and one exit value. Integer controls use canonical 0/1 values
with the original zero-test polarity; Boolean controls use false/true.
Previously observed calls stay in order.
The original zero-iteration exit is explored by ordinary symbolic execution;
the accelerated suffix also admits zero further iterations. All finite,
defined continuations are covered before the ordinary residual is removed.
This proves postconditions on terminating executions; it does not prove loop
termination. Nondeterministic data calls and other calls remain outside the
path accelerator's profile.

Recognized nondeterministic header controls select the sequence-aware path
accelerator automatically, bypassing the legacy whole-loop recurrence route.
Incoming scalar values must also be proved defined before acceleration.

For differential testing, set `ARITHEXE_FORCE_PATH_EXPRESSIONS=1` to bypass the
whole-loop fast path.  This is used by the path-expression regression tests; it
is not required in normal operation. Candidate generation can be configured
with `ARITHEXE_PATH_MAX_ROOT`, `ARITHEXE_PATH_BEAM`, and
`ARITHEXE_PATH_EVIDENCE` (defaults: 4, 4, and 2).

### Property-directed continuation pruning

For assertion/unreach-call safety, the executor stops a state when neither its
remaining instructions nor any active caller continuation can reach an error,
an assertion, or an unresolved call. This also skips nonterminating error-free
suffixes: termination is not the property being checked. A conservative
instruction-level reverse CFG/call graph follows both symbolic successors but
only the taken edge of a literal constant branch. Defined callees, including
recursive call graphs, are included. Unknown/indirect/replaceable calls remain
relevant; only explicitly recognized benchmark oracles and a small intrinsic
whitelist are trusted. Pending assertion/error/unknown states are never pruned.

After the arithmetic simplification passes, constant terminators are folded and
unreachable blocks removed to a fixed point. This narrow cleanup does not
recreate switches or speculate instructions. LoopSimplify/LCSSA are restored
before analyses are cached. Error source coordinates are preserved before
optimization so correctness witnesses still work when all error calls disappear.
Verbose messages use `[property-prune]`; the final count reports discarded
continuations. Set `ARITHEXE_PROPERTY_PRUNING=0` for diagnostic ablations (this
does not disable the frontend's constant-edge cleanup).

The frontend also removes stores to certified unobserved scalar globals before
the final DCE/loop-form restoration. A candidate needs a unique initialized
definition and exclusively direct, non-volatile, non-atomic store users: reads,
aliases, derived addresses, and escapes keep it live. The whole module must
have no unknown/indirect/replaceable calls, inline assembly, hidden EH/GC
callbacks, or unresolved memory-access bases. Explicit verifier-oracle and
standard assertion/abort contracts permit the numerical benchmark setup.
This is a closed-program reachability optimization, not an optimization for
libraries with external observers of exported state after the entry returns.
The residue proof uses the same certification policy. Removing dead spill
stores followed by DCE can expose a fresh integer oracle's genuinely
control-only SSA use; the strict call recognizer is not relaxed. Diagnostics
use `[spill-cleanup]`. Set `ARITHEXE_UNOBSERVED_GLOBAL_CLEANUP=0` to disable the
frontend transformation for comparisons.

### Independent coarse union-star recurrence proof

The proposed path mode (forced path expressions or integer relaxation) has
two separate proof routes. Each loop first tries exact trace compression and
precise acceleration; deterministic nested summaries are also tried first.
Supported scalar loops receive a bounded trace-driven trial window (default
`ARITHEXE_PATH_MAX_ROOT * ARITHEXE_PATH_EVIDENCE` observed iterations, normally
8). A successful precise jump retains its ordinary coverage residual. If the
current candidates fail after the observation limit, or the precise profile is unsupported,
an independent coarse proof uses
`(p1|...|pr)*`, with maps from complete one-iteration enumeration, not merely
the observed trace. Its context contains the entry values and preconditions,
but no precise first-break constraints or coverage-complement conditions.
The coarse continuation runs in an isolated worklist from the saved unchanged
pre-PHI entry, not from the observed prefix or an exact coverage residual. If it proves all
reachable continuation obligations, it discharges that incoming state; exact
path exploration of the same state is unnecessary. Otherwise its entire
worklist, assertion results, diagnostics and speculative certificates are
discarded. The engine first tries degree 2; if synthesis, certification, or
the continuation proof is inconclusive, it retries from the unchanged loop
entry at degree 3. Finding a useful degree-2 relation alone does not stop this
retry. Only after both levels fail does the original entry resume ordinary
symbolic execution and trace-driven acceleration. Unrelated incoming states and pending siblings
are preserved. The normal compatibility mode retains its legacy route. This
scheduling change concerns the precise/polynomial routes; the existing
guarded-residue prepass remains unchanged.

The separate `COARSE` recurrence-worker request invokes the existing
polynomial-expression C-finite synthesis directly, without the
path-sensitive periodic solver. For non-nested loops, one-iteration enumeration checks pending
branch/select feasibility before collecting latch or exit states. Infeasible
combinations of correlated selects are removed only after an UNSAT result;
an UNKNOWN feasibility result rejects the summary (also in the legacy
summarizer), rather than silently dropping a possible transition.
Branch guards are deliberately dropped for
synthesis. Each returned relation is checked at iteration zero, and the
conjunction of the relations must be inductive under **every** body map
without assuming its guard. A satisfiable or unknown certificate query
rejects the optimization.

The exit state has a fresh nonnegative iteration count and fresh values for
all header PHIs, constrained by the certified observables. At count zero,
the PHIs retain their current incoming values. For a loop with only a header
exit and no internal assertion, the final header is replayed and its actual
exit guard is imposed. Otherwise a complete boundary iteration is replayed,
including internal assertions, break/return branches, intermediate updates,
and exit PHIs. Returning backedges are cut only inside the independent proof:
their prefixes are already covered by the certified union-star. All terminal
fragments must be proved in the same transaction; an inconclusive sibling
rolls back the entire attempt. For a positive count in a non-nested loop, the summary also
requires a predecessor satisfying the observables at count minus one and a
guarded body transition to the exit header state. This necessary last-step
condition preserves useful threshold precision. Earlier guards are not reconstructed:
this is an over-approximation of finite exits, not an exact word or a
termination proof. Any inconclusive assertion/error/feasibility query aborts
the independent proof transaction, rather than mixing its states with exact
execution. Coarse summaries are disabled on the saved precise replay.
`ARITHEXE_COARSE_REFINE_STEPS` limits the total replay
instruction steps per verification run (default 512; 0 disables refinement).
If replay also fails to decide the property within that budget, the result is
`UNKNOWN`, never an unvalidated `FALSE`. A violation found by the resumed
exact execution is reported normally; no exact trace is fabricated.

For example, arbitrary choices between `x++` and `y++`, followed by `i++`,
preserve `x+y-i`. The fallback proves `x+y==i` for the regression
`test/benchmark/path_expression/true_coarse_branch_sum.c`, even though it
cannot determine the branch sequence or individual values of `x` and `y`.

For a loop nest, coarse reasoning starts only at the **outermost** incoming
header. The complete region is flattened into polynomial scalar CFG-edge
maps, including inner body updates, PHI-edge copies, resets and stuttering
control edges. All internal guards and location/order restrictions are
forgotten: the language is `(edge_1 | ... | edge_m)*`, not a guessed finite
number of inner or outer iterations. Its count measures flattened steps.
PHI copy webs are coalesced only after SSA liveness/interference checks;
distinct simultaneously live values remain separate coordinates, and
non-PHI scalar values live across blocks are carried explicitly. Coordinates
not initialized at the outer entry start unconstrained in the header-only
route. Multi-exit/assertion replay gives non-entry SSA ghost registers a
canonical zero initial value: valid SSA uses cannot read them before their
definitions or predecessor-edge assignments. Actual outer inputs are always
taken from the incoming state. Jointly inductive entry-equality templates
are certified under every map, with invalid candidates repeatedly eliminated;
these inexpensive zero-growth degree-one observables strengthen both the
full degree-2 and degree-3 searches and remain available if a worker times out.
This over-approximation covers zero inner iterations and arbitrary
interleavings. For multi-exit/assertion replay, every block in the region is
checked from an arbitrary certified prefix, including inner assertions and
all exits; only the outer header can receive a zero-length prefix. Internal
CFG edges are cut inside the proof after their source block's obligations
are checked. Every edge map must pass the joint
inductiveness certificate. Extraction/resource failure rejects the whole
summary, never a subset of edges. Internal assertions are not skipped.
Identical edge maps are deduplicated. No guarded-last-outer-iteration relation
is imposed on the flattened language, whose steps are CFG edges. Zero-trip
equalities are separate constraints rather than distributed nonlinear ITEs.
Fermat-1 is proved at degree 2 using the invariant
`u*u - v*v - 2*u + 2*v - 4*r == 4*A`, without solving inner exit counts.

The supported profile is side-effect-free scalar loops, including multiple
guarded exits and internal assertions. Pure fresh Boolean body calls may select
maps, and recognized Boolean header controls permit arbitrary finite
iteration counts. The independent coarse route also admits recognized fresh
header controls with internal exits; precise arbitrary-finite header-exit
jumps retain their stricter admission rule. Loops containing memory effects
and nondeterministic data-dependent updates are
conservatively rejected. No modulo or bit-vector semantics are removed:
non-polynomial fixed-width updates retain ordinary execution. Proofs in
integer-relaxed mode remain proofs of that relaxed model only.

The coarse proof always uses the saved incoming preheader state even when it
is scheduled after observing precise iterations. Each sequential loop and
each new invocation has its own history and trial window. Later loops inside
an already independent coarse transaction stay on the coarse proof route.
`ARITHEXE_PRECISE_TRIAL_ITERATIONS` changes the observation budget; `0` is an
explicit eager-coarse ablation. This bounds observed iterations, not total
solver time or the number of forked states. Later precise candidates remain
enabled if both degree levels fail. `--verbose` shows precise trial deferral,
profile rejection/window exhaustion, attempted degree, coarse language, observables, step certificates,
independent proof discharge/rollback, and resumption of precise execution.
Set `ARITHEXE_COARSE_FALLBACK=0` for precise-only comparisons, or
`ARITHEXE_COARSE_TIMEOUT_MS` to change its worker timeout (default 3000 ms,
capped by `ARITHEXE_SOLVER_TIMEOUT_MS`). This also bounds each engine
assertion/feasibility query inside the independent proof (ordinary queries
are unchanged). `ARITHEXE_COARSE_PROOF_STEPS` bounds the independent proof's
processed states (default 2048; 0 immediately abandons it).
`ARITHEXE_COARSE_POLY_EXPR_STRATEGY`,
`ARITHEXE_COARSE_POLY_EXPR_ORDER`, and `ARITHEXE_COARSE_POLY_EXPR_DEGREE`
select the coarse synthesis strategy, recurrence order and polynomial degree,
independently of the legacy CLI defaults. Defaults are `auto`, order 1, and
the degree-2 then degree-3 proof ladder. Setting
`ARITHEXE_COARSE_POLY_EXPR_DEGREE` explicitly pins a single degree instead.
Degrees are sent per worker request and included in cache keys, so a
persistent worker cannot reuse a degree-2 result for degree 3. Summary,
total attempt, and per-degree attempt counts are exposed by
`VerificationSession`.

Coarse initial-definedness/base/step certificates and the independent coarse
continuation's assertion and feasibility queries also include the seven
arithmetic lemmas in the root `lemma.smt2`. The file is parsed once per
verification session and re-added after each solver reset. These are
**trusted assumptions**, not theorems proved by ArithExe; changing them can
affect proof soundness. Ordinary precise queries and polynomial-expression
coefficient synthesis do not receive them. Verbose mode logs the loaded file
and lemma count. Missing or malformed files reject the coarse attempt.

Quantified lemmas can obstruct SAT search. Each coarse query first tries the
augmented solver for at most 1000 ms (or its shorter query budget), then on
UNKNOWN retries the original assertions without lemmas using only the
remaining budget. No lemma constraints leak into precise fallback.
`ARITHEXE_COARSE_LEMMA_FILE` selects an explicit file; otherwise a copy beside
an explicitly configured recurrence worker is preferred over the repository
default. CMake and benchmark snapshots include the file, and Linux comparison
configurations fingerprint it alongside the Python solver support files.

Run the integration tests with:

```sh
./build/test_engine --gtest_filter='CoarsePathFallback.*'
python -m unittest discover -s test -p test_coarse_solver.py
```

### Independent guarded residue proof

For nondeterministic scalar loops whose safety depends on modular guards,
`ResidueProof.cpp` adds a separate proof-only route. Its automatic profile is a
no-argument entry function containing a natural loop with a recognized fresh
nondeterministic call and an unsigned remainder by a non-power-of-two constant
between 2 and 16. It is attempted independently before the ordinary worklist;
failure leaves the precise and polynomial-recurrence routes unchanged.

The procedure constructs exhaustive residue and zero/nonzero predicates over
live SSA values at loop headers. It symbolically executes acyclic paths between
headers, preserving exact branch guards, selects and parallel PHI assignments,
and uses SMT to enumerate every feasible successor predicate cell. Closure of
the finite worklist is an inductive over-approximation. Every assertion and
error call on these paths must be proved unreachable. A feasible abstract error
does **not** establish a counterexample: it rejects this route and falls back.
Thus it handles `mod3`'s conditional invariant `y == 0 => x % 3 == 0`, which a
guard-free polynomial union-star summary cannot retain.

This is guarded residue reasoning, not precise path-word acceleration or
polynomial recurrence synthesis. It retains machine-width wrapping, casts,
signed comparisons and bounds even with `--ignore-bitwidth-constraints`.
Each nondeterministic call is a fresh bounded value; calls used as data are not
frozen. The supported scalar operations include integer addition, subtraction,
multiplication, unsigned division/remainder, comparisons, casts, selects and
Boolean operations. Potential `nsw`/`nuw` overflow is rejected rather than
silently assumed away. Scalar global loads are allowed only when all uses of
that global are direct nonvolatile/non-atomic loads of its initializer type.
Reducer spill stores may be ignored only when **all** uses of the target global
are direct nonvolatile/non-atomic stores: there can be no read or escaping
address. Other memory effects, unsupported instructions/calls, irreducible
cycles, exhausted budgets and solver UNKNOWN retain the ordinary state.

Verbose diagnostics use `[residue-path]`; session counters distinguish attempts,
queries and proofs from polynomial summaries and precise accelerations. Set
`ARITHEXE_RESIDUE_FALLBACK=0` to disable only this route, or
`ARITHEXE_COARSE_FALLBACK=0` to disable both independent coarse routes.
`ARITHEXE_RESIDUE_TIMEOUT_MS` defaults to 3000, with each SMT query limited to
200 ms or the remaining time. `ARITHEXE_RESIDUE_MAX_STATES` defaults to 2048 and
`ARITHEXE_RESIDUE_MAX_QUERIES` to 8192. Supported-profile checking precedes these
execution/query budgets. No trusted polynomial lemmas are used by this route.

Correctness-witness emission remains available, but it currently exports the
existing error-location invariants, not the internal residue fixed-point
certificate. Such witnesses have not been independently validated.

```sh
./build/test_engine --gtest_filter='GuardedResidueFallback.*:CoarsePathFallback.ProjectsControlOnlyIntegerSelects'
```

### Temporary summary-only integer constraint bypass

`--ignore-bitwidth-constraints` is an opt-in, experimental **summary-only
IR-level integer relaxation** for all integer widths. `--ignore-32bit-constraints`
remains an alias. Unlike the earlier global bypass, ordinary symbolic execution
always uses fixed-width LLVM/C integer semantics, including fallback and
counterexample replay. For example, from the
repository root:

```sh
./build/arith_exe \
  --ignore-bitwidth-constraints --witness=threshold-relaxed.yml \
  test/benchmark/path_expression/true_unsigned_large_threshold.c
```

Internal summary probes use Int arithmetic without machine-width normalization, result bounds,
`nsw`/`nuw` overflow obligations, or bitvectors. This applies to arithmetic,
signed/unsigned comparisons, division/remainder, integer casts, stores,
recursive-summary input bounds, and allocation size arithmetic **inside those
probes only**. Ordinary instructions retain wrapping/narrowing, signed and
unsigned comparisons, casts, bitwise/shift semantics, and LLVM `nsw`/`nuw` and
definedness checks. Unsigned overflow wraps rather than being excluded by an
assumed no-overflow condition. Recognized
SV-COMP nondeterministic integer inputs retain their finite source-type domains:
unsigned APIs (`uchar`, `ushort`, `uint`, `ulong`, `ulonglong`) get
`0 <= input < 2^w`; signed APIs (`schar`, `short`, `int`, `long`, `longlong`)
get `-2^(w-1) <= input < 2^(w-1)`. The compiled return width `w` respects the
selected data model. Bounds are linear Int constraints, applied on every call,
including fresh inputs sampled during ordinary loop execution and loop probes.
These input bounds are linear; ordinary arithmetic can also generate
modular/bitvector formulas. Source API signedness prevents accidental exclusion
of the upper half of unsigned input domains by generic signed IR bounds.
Plain `char` uses ABI sign/zero-extension attributes when available; otherwise
its conservative domain is the union of signed and unsigned ranges. Boolean
inputs remain Boolean (or 0/1 for integer-declared Boolean APIs). Unknown
external APIs are not assigned a guessed signedness. Acceleration of loops
with data-valued external calls remains unsupported in the proposed mode;
such loops fall back to ordinary symbolic execution.
Within relaxed probes, unsigned comparison literals and zero-extended literals are decoded as unsigned
bit patterns, without adding modular constraints.
Signed division/remainder truncate toward zero; unsigned
division/remainder use integer div/mod without operand normalization. Integer
casts in probes preserve the value. Booleans remain Boolean: zero extension maps to 0/1,
sign extension to 0/-1, and truncation to Boolean keeps the low-bit parity.

In relaxed probes, Boolean logic is supported directly. Integer `&`, `|`, and `^` with a constant
mask use unbounded two's-complement integer formulas. Constant shifts use
multiplication/division by powers of two; both right-shift variants use floor
division, with no machine-width cap or unsigned zero-fill boundary. Symbolic
bitwise masks and symbolic shift amounts report `UNKNOWN` instead of generating
bitvector formulas. Constant masks have a 4096-binary-digit resource limit,
and constant shift amounts must not exceed 4096. Exceeding these limits also
reports `UNKNOWN`, not a solver-side bound. These probe restrictions do not
apply to ordinary fixed-width execution, which supports symbolic bitwise
operands and shifts through the existing bitvector encoding.

Actual program remainder operations (e.g. `% 2`) are retained. Division by
zero, negative shifts, arithmetic exactness, operand definedness, and memory
safety conditions are retained, as are path guards, first-break minimality,
and assertion checks. Configuration is per verification session and disabled
by default. The option selects the path-expression accelerator instead of the
legacy whole-loop route. Optional quantifier elimination has a two-second
timeout and falls back to the exact relational repetition count.

Results are explicitly labeled `TRUE(integer-relaxed)` or
`FALSE(integer-relaxed)`. Witness generation is enabled by default, including
in this mode; `--witness=PATH` chooses the output and `--no-witness` disables it.
Relaxed witnesses include a warning comment and producer configuration
`summary-integer-relaxed-fixed-width-execution`. Neither the result nor the witness establishes the
corresponding fixed-width C result without independent validation. This is not
a complete mathematical-integer C frontend: compilation/optimization and LLVM constants
still reflect C/LLVM semantics. Frontend-folded constants or branches cannot be
recovered by the backend relaxation. Memory layout remains the compiled layout.
Use this bypass primarily for simple scalar polynomial loops with
ordinary-sized constants. Exact C verification needs a separate
no-wrap/definedness certificate; that automatic certification is not
implemented by this option.

## Google test
Beside used as unit tests,
google test is also used to benchmark this verifier.
In `test/benchmark`, we have included some programs that this tools can verify.
To generate test case based on them automatically,
type the following commands
```
cd test
python gen_test.py
```
This will generate `test/test_logic.cpp`, which includes test cases.
Rebuild this project and go to `build/test` to run `ctest`.

`gen_test.py` scan recursively all `.c` in benchmark folder, so feel free to add your own benchmark program in it.
# Bounded precise-summary queries

After precise acceleration, feasibility and assertion queries have an internal
3-second solver timeout. Before attempting a quantified query, the engine may
search for small repetition counts (0 through 8) with positive universally
quantified conjuncts temporarily omitted. These are **candidate models only**:
each candidate is pinned and checked again against the complete original
formula, including all branch guards and C overflow/definedness constraints.
Only a satisfiable original formula can establish a feasible execution or
counterexample. An unsuccessful candidate search cannot prove safety; the
engine then queries the original formula, and an unresolved query yields
`UNKNOWN`, not `TRUE` or `FALSE`. Ordinary execution before acceleration is
unchanged. This is not the integer-relaxation bypass.
# Final-query rational normalization

Recurrence workers export a separate exact-rational candidate alongside the
legacy integer representation. Precise and coarse accelerators use that candidate
only after mathematical base/step certification. At an accelerated state boundary,
an integer value is bound by denominator-cleared equality (`d*v = P`), not by
quotient/remainder bounds. Nested jumps use the same representation. Actual C
integer division/remainder and uncertified legacy consumers are unchanged.

Precise first-break inference reuses `LinearLogic::solve_vars` on supported
quantifier-free linear integer formulas, including equalities implied by strict
integer bounds. Inferred counts are certified against the original first-break
relation before being substituted into closed-form state values and the reduced
exit domain. Unsupported or inconclusive inference retains the relational count.
The legacy linear matrix solver requires exact integer pivots, so this optional
inference currently admits only unit coefficients of the repetition count;
non-unit stride constraints retain the relational fallback.
Piecewise (`ite`) solutions also retain the relational representation rather
than expanding potentially large conditionals into every closed-form value.

Assertion queries are postprocessed before Z3: exact rational polynomial
comparisons over integer terms are converted by positive-denominator clearing.
Constant nonzero integer `div`/`mod` is replaced by an existential quotient `q`
and `0 <= a - d*q < abs(d)`. This preserves integer rounding, including negative
operands and denominators; it does not treat integer division as rational division.
Auxiliaries are local to each quantifier, and original free input symbols remain
available for counterexample models. Real-valued variables, symbolic denominators,
and division by zero retain their original semantics. This is an exact arithmetic
preprocessing step, not a complete nonlinear solver. Precise accelerated queries retry the original equivalent encoding
if normalization returns `unknown`, preserving the existing counterexample
validation fallback.

Unresolved quantified precise queries and residual-coverage checks additionally
try Z3's `simplify -> qe -> smt` strategy. This fallback has its own bounded
budget (3 seconds for coverage, the configured query timeout for precise
queries). It runs on the full original assertions and accepts only `unsat`;
`sat`, timeout, or unsupported tactics leave the original `unknown` unresolved.
The original solver and its counterexample-model handling are unchanged. Verbose
mode reports the fallback outcome under `[quantified-query]`. This resolves the
quantified residual coverage in `loops-crafted-1/watermelon.c`; it is not a
general completeness guarantee for quantified nonlinear arithmetic.

Branch feasibility and safety obligations are separate. An unknown feasibility
check retains the successor with all of its original constraints; it does not
permanently make the verification result unknown. Expression/call case pruning
also removes cases only after UNSAT. Assertions and explicit error calls still
require their full violation query to be UNSAT for a safety proof (an unresolved
error query remains UNKNOWN).

After constructing a precise jump with quantified coverage, a restricted independent finite-exit safety
proof is tried before querying residual coverage. Quantifier-free coverage uses
the precise jump directly, preserving relations across sequential loops.
The same optional proof is tried at entry when a body call prevents precise
acceleration. Known fresh oracles used only as branch/select controls are
allowed, including correlated Boolean selects and old-style declarations.
The polynomial coarse route also accepts header Boolean PHIs whose incoming
values are fresh `__VERIFIER_nondet_bool()` calls and whose uses are exclusively
branch/select conditions. It enumerates the correlated branch choices, projects
these Boolean coordinates out of each transition guard, and solves recurrences
over the remaining integer coordinates. Boolean data uses and persistent flags
are not admitted by this recognizer. Exit controls preserve their entry value
for zero trips and are fresh choices after positive-length executions.
Guarded non-negativity candidates strengthen coarse summaries only after Z3
certifies their entry conditions and joint inductiveness over every guarded
transition; a decrement that can overshoot zero cannot acquire this invariant.
The optional probe yields after at most 64 execution steps (or the smaller
`ARITHEXE_COARSE_PROOF_STEPS` limit), or 250 ms checked between operations;
in-flight solver queries retain their separate timeout. Exhaustion is
inconclusive, never a safety proof, and resumes the saved exact execution.
It havocs scalar header PHIs
and retains the actual exit guard.
This summarizes all possible finite exits without claiming that the loop
terminates. The fallback supports only nonnested, side-effect-free integer
loops with one header exit and no internal calls/assertions other than these
control-only oracles; body-only live-outs
are rejected. A successful proof covers the unchanged loop entry, not just an
exact summary's residual. An inconclusive proof rolls back and retries the
unchanged exact trial and its coverage query. Polynomial-summary scheduling
remains available as before. Exit-guard-only proofs do not load polynomial
lemmas; later polynomial summaries retain their existing lemma handling. SAT under this
over-approximation is not reported as a real counterexample.
Set `ARITHEXE_EXIT_GUARD_FALLBACK=0` to disable this optional safety summary.

Unconditional solvable polynomial recurrences use the direct layer solver,
with canonicalized transition indices (`n+1` and `1+n` share a key).
Fallback worker-side inductiveness checks have a 3000 ms per-query limit;
`ARITHEXE_RECURRENCE_CERT_TIMEOUT_MS` overrides it (minimum 1 ms).
Timeout or UNKNOWN rejects that candidate and retains the existing fallback
routes; it never certifies a recurrence.

Pure nondeterministic header controls also support certified stable periodic
words. For a word of length `d`, the closed form uses `q = n / d` complete
repetitions and the proper prefix at offset `r = n % d`. Every offset's
branch guards and competing-path exclusions are checked, and the oracle
witness records `n` continue calls followed by one exit call. Finite transient
phases still use ordinary execution until a stable periodic suffix is reached;
this extension does not yet symbolically accelerate those phases.

Loop acceleration can summarize pure, acyclic scalar helper functions on
demand. It symbolically executes their finite return paths, proves exhaustive
defined return coverage, and constructs a parametric guarded return expression.
Nested pure helpers are supported; recursion, memory effects, external calls,
and unsupported instructions retain ordinary call handling. Mathematical
helper summaries are cached separately per verification session and used only
by integer-relaxed summary probes, never by fixed-width ordinary execution.

Straight-line scalar-global helpers additionally have explicit state-transformer
summaries: scalar arguments and old global values are inputs, updated global
values and an optional scalar return are outputs. Thus `void foo(){ x--; }`
becomes `x' = x - 1`, rather than an unsupported void-return call. These
mathematical transformers are cached separately per session. Ordinary execution
still interprets the body with C semantics, and global-state functions are not
memoized using only their scalar arguments.

Precise loop acceleration can carry a directly accessed scalar global through
a header PHI whose latch input is the final direct global load. It certifies
the incoming global/PHI equality, uses symbolic global state during body
exploration, and restores actual global memory after the jump. All helper globals
must be tracked; unmapped globals, pointers, volatile/atomic accesses, conditional
or recursive helpers, and nondeterministic-header global loops are outside this
initial profile. Coarse global summaries are not enabled.
