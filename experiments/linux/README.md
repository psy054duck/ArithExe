# Linux comparison experiment

The runner uses BenchExec 3.35's execution and tool-info APIs. Run it inside a
systemd service delegated to the unprivileged experiment user, with `Delegate=yes`
and `DelegateSubgroup=manager`. Do not run the tools as root. Linux cgroups must
support CPU, memory and freezing; the runner activates CPU/memory only within its
own explicitly delegated `path-expression-*.service`. Missing controls cause a setup failure, not a
silently unbounded experiment.

Use the same 619 processed sources, source hashes, final assertions, per-task
ILP32/LP64 models and unreach-call property. Defaults are 30 seconds **wall time**,
1 GiB **hard cgroup memory** including all descendants, and **one job at a time**.
Unlike the historical Mac baseline, memory is not a sampled RSS watchdog. This
is an exploratory comparison, not official SV-COMP resource limits or scoring.

ArithExe is the forced path-expression, integer-relaxed mode with finite input
bounds and witnesses enabled. ICRA uses CRA/Newton with loop splitting, and
reports an unproved assertion as UNKNOWN, never FALSE. Tool crashes and resource
limits take precedence over proof-like strings in unfinished output. VeriAbs
and Automizer commands/results use the official BenchExec modules.

Official SV-COMP 2026 packages:

- Automizer: <https://zenodo.org/records/17735224>, `--full-output`.
- VeriAbs: <https://zenodo.org/records/10243500>, `--sv22`. The competition reuses
  its SV-COMP 2024 archive as an inactive tool, not the newest standalone version.
- Registry: <https://sv-comp.sosy-lab.org/2026/systems.php>.

A configuration JSON provides an absolute `dataset` path and enabled tool records
named `arithexe`, `icra`, `automizer`, each with `executable` and an
optional `environment` mapping. ICRA additionally needs `loadpath`; VeriAbs and
Automizer need `options` arrays. Never include credentials in this configuration.
VeriAbs is held pending permission for this modified dataset; `configure.py`
deliberately omits its executable. Do not add it to the run until permission is
resolved.

```
python benchmark.py --config /absolute/config.json --output /absolute/results/pilot --tools arithexe icra automizer --limit 4
python benchmark.py --config /absolute/config.json --output /absolute/results/full --tools arithexe icra automizer
python benchmark.py --config /absolute/config.json --output /absolute/results/full --tools arithexe icra automizer --resume
```

Keep the tool archives, checksums, dependency inventories, source revisions and
compatibility patches alongside results. Resume requires identical dataset,
runner hash, platform, tool configuration and limits. Each completed task is
durably journaled; `results.csv` and `summary.json` are refreshed after each task.
Separate output directories retain witnesses, prepared sources, commands, logs
and raw BenchExec measurements. Full output is capped at approximately 16 MiB
per task; BenchExec retains the start and end of truncated logs.

Original verdicts are **provenance only**, because internal assertions were
removed and tools' arithmetic models differ. Nominal label matches are not
validated correct predictions. No witness validation is claimed by this runner.

## Prepared Ubuntu environment

The current server experiment root is `/home/ubuntu/path-expression-experiment`.
It uses Ubuntu 26.04 x86-64, two virtual CPUs and about 2 GiB RAM. LLVM/Clang
20.1.8 are required by the current ArithExe debug-record API. ArithExe is a Debug
build with Z3 4.15.3 and SymPy 1.13.3, installed in an isolated Python 3.14 venv.
ICRA uses an isolated OCaml 4.14.2 switch, the recorded upstream compatibility
patch, and the library-only WALi build patch in this folder. GNU linking must
retain Duet (`--no-as-needed`) and use the same pip Z3 as the OCaml bindings.
Automizer uses Java 21; ICRA uses Java 11 for its BDD support. Both receive
`_JAVA_OPTIONS=-Xmx768m -Xss4m` within the common 1 GiB limit.

After building all three tools, generate `configuration.json` with
`venv/bin/python ArithExe/experiments/linux/configure.py "$experiment_dir"`.
The configuration fingerprints executables, linked libraries, Python support
sources and Automizer's unpacked distribution. Do not rebuild or modify tools
during a run. Retain `logs/*inventory.txt`, the source archives, configuration,
and all per-task measurements. Pilot smoke tests are setup checks, not final
performance measurements.

Start the full run as an unprivileged, delegated transient service; it survives
SSH disconnection:

```bash
experiment_dir=/home/ubuntu/path-expression-experiment
sudo systemd-run --unit=path-expression-benchmark-full --uid=ubuntu \
  --working-directory="$experiment_dir" \
  --property=Delegate=yes --property=DelegateSubgroup=manager \
  --property=StandardOutput=append:"$experiment_dir/logs/full.log" \
  --property=StandardError=append:"$experiment_dir/logs/full.log" \
  "$experiment_dir/venv/bin/python" \
  "$experiment_dir/ArithExe/experiments/linux/benchmark.py" \
  --config "$experiment_dir/configuration.json" \
  --output "$experiment_dir/results/full" --tools arithexe icra automizer
```

Check `results/full/summary.json`, `logs/full.log`, and
`systemctl status path-expression-benchmark-full`. A successful full run writes
`results/full/completed.json` after all 1,857 jobs and final fingerprint checks.
After interruption, use a new service name and the same command with `--resume`.
