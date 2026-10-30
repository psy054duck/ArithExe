# Local reducer diagnostics

`benchmark_reducers.py` selects every included task whose name contains
`-reducer` from the single-assertion dataset. It reuses `run_tool`, process-group
sampling/cleanup and snapshot creation from the existing macOS benchmark
runner; it does not modify that historical runner or any earlier results.

```sh
/opt/miniconda3/envs/veri/bin/python experiments/local/benchmark_reducers.py \
  --output ../svcomp2026-single-assertions/benchmarking/reducers-local-NEW \
  --timeout 30 --jobs 4
```

Default settings match the local proposed-mode diagnostics: forced path mode,
six-symbol root bound, beam 4, evidence 2, independent coarse/residue routes
enabled, polynomial degree escalation 2 then 3, 30-second wall limit, four
workers, witnesses and verbose logs. The 1 GiB process-tree memory limit is a
sampled macOS RSS watchdog, **not** a hard cgroup allocation limit. Use
`--limit 3 --jobs 1` for the three mod3 smoke tests. `--mode fixed-width` removes
the experimental summary integer-relaxation flag. Specify the Python/Clang
paths when these defaults do not match your installation.

Each run has a fresh directory with a frozen binary, solver/source files,
dataset and hashes, configuration, durable `results.jsonl`, `results.csv`,
`summary.json`, and individual logs/commands/witnesses under `runs`. A finished
run writes `completed.json`. Resource sampling requires permission to inspect
local processes; the script fails before starting tools if this is unavailable.
Cleanup targets only the process groups created by the runner. It never runs
the C benchmark programs themselves.

For the repair workflow, select the exact reducer tasks reported TRUE by at
least one ICRA configuration and stop at the first ArithExe failure:

```sh
/opt/miniconda3/envs/veri/bin/python experiments/local/benchmark_reducers.py \
  --icra-results ../svcomp2026-single-assertions/benchmarking/reducers-local-20261005/icra-baseline-results.jsonl \
  --stop-on-failure --jobs 1 --timeout 30 \
  --output ../svcomp2026-single-assertions/benchmarking/reducers-icra-NEW
```

Selection accepts only `icra_split`/`icra_refine` TRUE records and checks each
selected source hash and data model against the local manifest. Tasks run in
sorted task-ID order, with no queued parallel work in stop-on-failure mode.
The first non-TRUE result writes `stopped.json` and records the untested count
in `summary.json`; it does not claim that all selected tasks were completed.
`completed.json` is reserved for reaching the end of the selected set.

Summary counts are **reported** verdicts, not validated correct predictions.
Original parent labels are provenance only. `all_assertions_reported_true`
means every selected assertion variant from that parent returned TRUE; it
does not assert that the original program or its removed annotations have been
verified. Witnesses are generated but not independently validated. Local
concurrent wall times are diagnostic measurements, not controlled speedups or
direct comparisons against the Ubuntu experiment.
