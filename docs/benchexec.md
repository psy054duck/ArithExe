# BenchExec experiments

`tools/run_benchexec.py` runs array-fpi through BenchExec, not through the older
custom subprocess runner. `benchmark_tools/arithexe.py` implements BenchExec's
`BaseTool2` interface. The solver itself and the original pivot implementation
are unchanged by this integration. ECCE is still a separate experiment, not the
tupling backend used by this runner.

## Installation

The Ubuntu workspace uses BenchExec 3.34, built from upstream release commit
`829bf7a4583e217323b8b7c3c1db3de1fd175d52`. Its dependencies are isolated from the
solver environment. From the workspace directory containing `ArithExe`:

```sh
git clone --depth 1 --branch 3.34 https://github.com/sosy-lab/benchexec.git third_party/benchexec
python3 -m venv benchexec-venv
benchexec-venv/bin/python -m pip install ./third_party/benchexec
```

The runner requires Python 3.11 or later and the existing `solver-venv`, which
contains ArithExe's Python dependencies. It verifies solver imports before
starting an experiment. Interpreter paths retain the virtualenv executable
symlink instead of resolving it to a system interpreter.

## Running array-fpi

On the prepared Ubuntu server, from `ArithExe`:

```sh
../benchexec-venv/bin/python tools/run_benchexec.py \
  --scope system --svcomp ../svcomp25 \
  --solver-python ../solver-venv/bin/python \
  --output-dir ../results/benchexec-array-fpi-300s
```

For a persistent run that survives closing the terminal or SSH connection:

```sh
../benchexec-venv/bin/python tools/submit_benchexec.py \
  --unit pivotrec-array-fpi --output-dir ../results/benchexec-array-fpi-background
```

Additional options (such as `--task eqn1f` or `--strategy pivot`) are forwarded
to the runner. The submitted service runs the wrapper, which creates a separate
delegated execution scope with `--scope system`. The execution scope is bound
to the wrapper service, so stopping the wrapper also stops its benchmarks. Follow
`../results/benchexec-array-fpi-background.service.log` or use
`systemctl status pivotrec-array-fpi.service`. To cancel, use
`sudo systemctl stop pivotrec-array-fpi.service`. Tables and the final summary
are generated automatically at completion. The temporary unit is collected,
while result files and the service log remain available.
Swap is disabled for this job's process tree, not for the rest of the host.

Defaults are the `tupling` strategy, 300 CPU seconds per task, a 360-second wall
limit, 3 GB of memory, one CPU core per task, and two concurrent tasks. Each
solver request has a 300,000 ms wall timeout, still subject to the total task
limits. These limits apply to the whole process tree, including Python workers.
All runs use the fixed-width C semantics and data model from the SV-COMP YAML
task, without integer-relaxation flags. Witness generation is disabled to avoid
shared output files; witness validation is not part of this experiment.

Use `--task eqn1f` for a smoke test, repeated `--task` arguments for a subset,
`--strategy pivot` for the original route, or `--prepare-only` to inspect the
generated XML and manifest without executing any task. Resource limits can be
changed with `--timelimit`, `--walltimelimit`, `--memory`, and `--workers`.

The server blocks unprivileged user namespaces, so runs use `--no-container`.
The `--scope system` option creates a temporary delegated systemd service using
passwordless sudo. The service and every benchmark run execute as the calling
user, not root. The service is collected at exit; no global systemd configuration
is changed. CPU pinning and cgroup memory/CPU accounting remain enabled.
On machines that already delegate all necessary controllers to the user, use
`--scope user` (the default). `--scope none` requires an existing suitable cgroup.
See the upstream [cgroup setup instructions](https://github.com/sosy-lab/benchexec/blob/3.34/doc/INSTALL.md).

## Outputs

Each experiment has a new output directory; existing results are never replaced.
It contains:

- `array-fpi.xml`: the exact benchmark definition using YAML task files and the
  reachability property, so BenchExec retains expected verdicts and data models.
- `manifest.json`: revision/branch, dirty status, source and binary hashes,
  solver runtime/package versions, settings, command, and individual task hashes.
- `source.tar.gz`: tracked and non-ignored untracked source files at launch.
- Native BenchExec result XML and per-task log files, including CPU time,
  wall time, peak memory, exit status, and expected-verdict classification.
- `summary.json`: category/status counts and all per-task measurements.
- `tables/`: native BenchExec HTML and CSV tables.

The adapter recognizes only exact standalone verdict lines and does not count
crashed, timed-out, memory-killed, or missing-dependency runs as successful
proofs. Native `wrong` categories are preserved, not relabeled as successes.
An agreement with an expected verdict is not independent witness validation.
The wrapper exits nonzero for incomplete execution or wrong answers.

Integration tests:

```sh
../benchexec-venv/bin/python -m unittest discover -s test/benchexec -v
```
