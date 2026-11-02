# Recurrence postprocessing optimizations

The performance checkout is isolated from the original `ArithExe` directory
while its full array-fpi experiment is running. Its branch is
`feature/recurrence-performance`; `core/multivariate.py` remains unchanged.

## Changes

Periodic, piecewise, and symbolic closed forms cache their dictionary exports.
Cache keys retain snapshots of the input AST references and dictionary items,
so edits through the existing mutable lists or nested dictionaries invalidate
the result. Exports return dictionary copies; callers cannot poison a cache by
editing a returned mapping. There is no process-global AST cache or cross-context
reuse. Integer-division simplification and its equivalence check are preserved.

Symbolic closed forms now compute their merged flattened view only when that
view is requested. Ordinary solving, substitution, and export do not consume
that view. A requested flattened view still merges equivalent result cases,
but retains the exact disjunction of their guards as a compact Boolean formula
instead of eagerly converting it to DNF twice. The raw guarded export and
closed-form certification are unchanged.

The C++ case-merging helper now checks syntactic equality first and invokes
`solver.check()` once, not twice, for the same equivalence query. Only `unsat`
permits a semantic merge; the existing per-query timeout remains in force.

## Validation

The Python suite has 52 tests, including 15 export/laziness regressions. They
cover nested mutation, returned mappings, separate contexts, substitutions,
negative integer division, exact merged guards, and the existing original-equation
certificates. Two additional C++ tests check piecewise-merge semantics.

On Ubuntu, run the validation driver from the optimized checkout:

```sh
../benchexec-venv/bin/python tools/validate_performance.py \
  --baseline ../ArithExe --output-dir ../results/performance-comparison \
  --wait-for-unit pivotrec-array-fpi-tupling-full-20261030.service
```

It waits for the original experiment, builds the optimized checkout in Debug
mode, runs regression tests, and uses BenchExec for both versions on `brs1`,
`brs1f`, `eqn1f`, `indp2`, `condm`, and `mods`, with identical 300 CPU-second,
360 wall-second, 3 GB, two-worker settings. It saves native logs/XML/tables and
`comparison.json`. Speedup ratios are reported only when both versions match
the expected verdict; timeouts and unknown results are retained explicitly.
The result is a selected-task comparison, not a whole-array-fpi speedup claim.

An initial local macOS recurrence-only check used three fresh processes per
version, alternating version order, with imports excluded from CPU timing:

| Recurrence | Baseline Median CPU | Optimized Median CPU | Ratio |
| --- | ---: | ---: | ---: |
| `eqn1f_composed.rec` | 31.279 s | 6.206 s | 5.04x |
| `arithmetic_progression.rec` | 4.413 s | 2.494 s | 1.77x |

For every pair, SMT proved equivalence of the exported equations. These numbers
are not end-to-end C verification timings and do not establish Ubuntu or
whole-array-fpi speedups. The persistent Ubuntu validation service is
`pivotrec-performance-validation-20261030.service`; its results are written
under `results/recurrence-performance-20261030/validation` in the workspace.
