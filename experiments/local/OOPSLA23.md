# OOPSLA 2023 periodic sampling baseline

Select the separate recurrence solver route with:

```sh
ARITHEXE_PERIODIC_STRATEGY=oopsla23 <usual ArithExe command>
```

The default `compression` strategy is unchanged. It is the author's experimental
sampling-with-compression extension, not the algorithm published in OOPSLA 2023.

The new route implements Algorithm 1's prose specification (longest repeated
suffix coverage, shortest period on ties, backward partial-copy extension),
Algorithm 2's doubling/generate/validate/first-violation restart, and Section 7.1's
single-parameter `S1^q S2 S3^omega` initial-region generalization. It reuses the
existing closed-form and SMT/QE machinery; it is not the original artifact.
Printed Algorithm 1 has inconsistent index/length expressions; the implementation
follows its accompanying prose, not those expressions literally.

The recurrence route does not fall back to newer direct/coarse solvers. Unknown
validation, incomplete sample coverage, and exhaustion of the 100-step sampling
budget raise failures. The engine now selects an isolated legacy periodic-summary
route in this mode, without the newer online compression or coarse summaries.
Setting `ARITHEXE_COARSE_FALLBACK=0` is also recommended for explicit configuration.

The integration currently supports deterministic scalar SSA loops with a unique
exit block. Loops with unknown per-iteration calls or memory stores are rejected
and use ordinary execution instead. These are implementation limitations, not
limitations claimed for the paper. Immutable assertion-string arrays are not
summarized in this scalar route.

Incomplete recurrence solutions are rejected. Closed forms undergo initial-state
and transition certification. Iteration counts retain their full first-exit
constraints; bounded quantifier elimination (one second) simplifies these when
possible and preserves the original formula otherwise. Loop-invariant signed
casts retain ordinary-execution semantics during probing.

The October 6 debugging run corrected the contradictory verdicts for
`for_bounded_loop1` and `cohencu-ll__assert03`. A three-second-per-task audit of
36 previously crashing tasks and these two disagreements recorded no crashes;
timeouts in this audit are not proof-coverage results. See
`../diagnostics/oopsla23-debug-20261006-202834/` for logs and outcomes.

The earlier 217-task timing comparison must be rerun: unnecessary immutable-array
summaries contributed substantial overhead, and verdict-related fixes also alter
the comparison. Do not use its reported speedup as an algorithm-only result.

Tests:

```sh
python -m unittest discover -s test -p test_paper_periodic.py
python -m unittest discover -s test -p test_oopsla23_integration.py
```
