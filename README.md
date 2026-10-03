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
leave the ordinary symbolic state unchanged.

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

Direct external `__VERIFIER_nondet_bool()` header guards also support arbitrary
finite repetition. The call must have no arguments, return a Boolean, and be
used only by the header branch, directly or through a single-use Boolean
negation (`while (!__VERIFIER_nondet_bool())` is supported). The header must
be the only loop exit, and must contain only PHIs, the call, its optional
negation, and the branch (apart from debug records).
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

including the definedness conditions of all scalar updates. A symbolic Boolean
sequence is retained for counterexamples: a lambda array containing `n`
continue values and one exit value. Previously observed calls stay in order.
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

### Temporary all-width integer constraint bypass

`--ignore-bitwidth-constraints` is an opt-in, experimental **IR-level integer
relaxation** for all integer widths. `--ignore-32bit-constraints` remains an
alias, but now selects this same all-width mode. For example, from the
repository root:

```sh
./build/arith_exe \
  --ignore-bitwidth-constraints --witness=threshold-relaxed.yml \
  test/benchmark/path_expression/true_unsigned_large_threshold.c
```

The mode uses Int arithmetic without machine-width normalization, result bounds,
`nsw`/`nuw` overflow obligations, or bitvectors. This applies to arithmetic,
signed/unsigned comparisons, division/remainder, integer casts, stores,
recursive-summary input bounds, and allocation size arithmetic. Recognized
SV-COMP nondeterministic integer inputs retain their finite source-type domains:
unsigned APIs (`uchar`, `ushort`, `uint`, `ulong`, `ulonglong`) get
`0 <= input < 2^w`; signed APIs (`schar`, `short`, `int`, `long`, `longlong`)
get `-2^(w-1) <= input < 2^(w-1)`. The compiled return width `w` respects the
selected data model. Bounds are linear Int constraints, applied on every call,
including fresh inputs sampled during ordinary loop execution and loop probes.
They do not add modular arithmetic or bounds on intermediate results.
Plain `char` uses ABI sign/zero-extension attributes when available; otherwise
its conservative domain is the union of signed and unsigned ranges. Boolean
inputs remain Boolean (or 0/1 for integer-declared Boolean APIs). Unknown
external APIs are not assigned a guessed signedness. Acceleration of loops
with data-valued external calls remains unsupported in the proposed mode;
such loops fall back to ordinary symbolic execution.
Unsigned comparison literals and zero-extended literals are decoded as unsigned
bit patterns, without adding modular constraints.
Signed division/remainder truncate toward zero; unsigned
division/remainder use integer div/mod without operand normalization. Integer
casts preserve the value. Booleans remain Boolean: zero extension maps to 0/1,
sign extension to 0/-1, and truncation to Boolean keeps the low-bit parity.

Boolean logic is supported directly. Integer `&`, `|`, and `^` with a constant
mask use unbounded two's-complement integer formulas. Constant shifts use
multiplication/division by powers of two; both right-shift variants use floor
division, with no machine-width cap or unsigned zero-fill boundary. Symbolic
bitwise masks and symbolic shift amounts report `UNKNOWN` instead of generating
bitvector formulas. Constant masks have a 4096-binary-digit resource limit,
and constant shift amounts must not exceed 4096. Exceeding these limits also
reports `UNKNOWN`, not a solver-side bound.

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
`integer-relaxed-all-widths`. Neither the result nor the witness establishes the
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
