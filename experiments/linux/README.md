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
ICRA also accepts an `options` array; omission preserves the baseline
`["-cra-split-loops"]`. For the separate restricted path-refinement comparison,
use `["-cra-split-loops", "-cra-refine"]`, not `-cra-prsd` (which selects a
different recurrence domain). `prepare-icra-refinement.py` checks the original
ICRA fingerprints and the retained integer-only dataset, then exclusively
creates a new configuration. Use an isolated copy of the runner and a new
output directory/service; do not modify or resume the baseline run.
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

Every run exports `peak_memory_bytes` and `peak_memory_mib` (the original
`memory` field is retained in bytes). This is the BenchExec cgroup peak for the
entire process tree, including charged cache, not peak RSS or Java heap usage.
Missing measurements are null and explicitly flagged, never substituted with
zero. Each tool's summary includes the minimum, median, nearest-rank p95 and
maximum peak memory, both for all runs and for proved-TRUE runs. The completion
marker also states whether all memory measurements are available. Do not pool
timeout/OOM peaks with successful-run peaks when comparing proof efficiency.

Original verdicts are **provenance only**, because internal assertions were
removed and tools' arithmetic models differ. Nominal label matches are not
validated correct predictions. No witness validation is claimed by this runner.

The separate `svcomp2026-single-assertions` dataset splits the 605 retained
integer programs into 1,038 one-assertion tasks. Configure its absolute path
as `dataset` for a new experiment/output directory. The runner journals
`parent_task`, `parent_assertion_count`, and `assertion_index`, and suppresses
nominal label-match scoring when `expected_verdict_valid_for_split` is false.
A parent's verdict must not be treated as ground truth for each of its checks.
Do not modify or resume an existing 605-task experiment with this dataset.

To compare ICRA switches separately, use tool configuration keys `icra_split`
with `options: ["-cra-split-loops"]` and `icra_refine` with
`options: ["-cra-refine"]`. Both use the ICRA command builder and result
classifier, but their commands, journals, and result directories stay separate.
Select `--tools arithexe icra_split icra_refine automizer`. This is distinct
from the earlier combined `-cra-split-loops -cra-refine` experiment.

For the memory-recording rerun, `prepare-memory-rerun.py --base-results
/absolute/finished/results --run-dir /absolute/new/run` exclusively creates a
new configuration containing only `icra_split`, `icra_refine`, and `automizer`.
It requires a finished, fingerprint-verified baseline and preserves its tool
options, environments, dataset, and limits. Copy the runner to this new run
directory, validate a pilot, then use the timeout and memory limits recorded in
`preparation.json` for the full run. Never overwrite or resume the historical
experiment with the new runner.

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
# Component-only reruns

Pass `--components nla-digbench nla-digbench-scaling --tools arithexe` to
rerun the retained integer-only NLA tasks. Use a new output directory for each
source snapshot; do not resume or overwrite results from a different build.
`prepare-arithexe-rerun.py` fingerprints an isolated rebuilt source archive and
inherits the previous run's settings, explicitly enabling coarse fallback.
