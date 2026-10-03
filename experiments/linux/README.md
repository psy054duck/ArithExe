# Linux four-tool experiment

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

A configuration JSON provides an absolute `dataset` path and four tool records
named `arithexe`, `icra`, `veriabs`, `automizer`, each with `executable` and an
optional `environment` mapping. ICRA additionally needs `loadpath`; VeriAbs and
Automizer need `options` arrays. Never include credentials in this configuration.

```
python benchmark.py --config /absolute/config.json --output /absolute/results/pilot --limit 4
python benchmark.py --config /absolute/config.json --output /absolute/results/full
python benchmark.py --config /absolute/config.json --output /absolute/results/full --resume
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
