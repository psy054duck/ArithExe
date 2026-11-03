# General Integer Folds

Enable a per-worker JSON operator registry using an absolute path:

```sh
export ARITHEXE_FOLD_OPERATORS="$PWD/docs/fold-operators.json"
export ARITHEXE_ENABLE_AGGREGATES=1
```

The sample registry declares maximum and minimum. Each entry has a unique name,
`sort: "Int"`, an SMT-LIB `combine` expression with only free integer operands
`a` and `b`, and optional `laws`. Supported law names are `associative`,
`commutative`, and `idempotent`. An optional integer `identity` is also checked.
Every claimed law must be proved by SMT within 200 ms; UNKNOWN, timeout, or an
invalid declaration rejects the configuration. No law is silently trusted.
Operators without laws are permitted, but their folds preserve execution order.
Maximum over unbounded integers has no integer identity; the program's seed is
not treated as an identity.

The recurrence solver matches all covered updates against `combine(state, body)`
using SMT, permitting accumulator-dependent branch guards but requiring an
accumulator-independent body. Other evolving state components must have solved
summaries. Candidate discovery is bounded to 32 bodies; matching is limited to
32 solver calls and a one-second deadline per attempted component. This is an
incomplete recognizer: unsupported updates conservatively fall back.
When array composition splits an input among branches, discovery can propose a
piecewise input from accumulator-independent guard conjuncts. This projection
does not establish correctness: every original guarded update must still match
the registered operation, and the original guard coverage must be certified.

The resulting exact partial closed form is a seeded, ordered fold. Its serialized
metadata includes its body, bound index, seed, free parameters, and pure binary
operation. The C++ consumer preserves this metadata and only expands bounded
small intervals (at most 32 elements, within a shared budget). Empty intervals
return the original seed. Generic folds never enter sum-linearity or
sum-permutation rules. Remaining symbolic folds are conservative uninterpreted
terms; counterexamples involving them are not reported as validated FALSE.

Symbolic normalization recognizes conditional entry-array accesses structurally,
not source-level swap/rotation patterns. It partitions the access expression's
ITE tree and certifies, for each piece, range containment, injectivity, and
surjectivity onto a reference interval. A piece with incomplete coverage is not
silently completed. Sum contributions preserve multiplicity; product
contributions multiply. Generic fold duplication additionally requires
independently re-certified associativity, commutativity, and idempotence, and
equal seeds. A seed is never assumed to be an identity.

The current symbolic rule is deliberately restricted: each nonempty syntactic
piece must cover the entire reference interval exactly once. Summands may apply
a common pure element-wise transform to an entry-array read, such as a parity
indicator. The transform must be independent of the current position outside
that read. Arithmetic position predicates distributed through a Boolean
summand are handled by a bounded structural partition, followed by a pointwise
certificate against the original expression. Existing additive position remainders are handled separately for
sums; this does not permit position-dependent weighting. Generic seeded folds
also admit same-interval permutations when their operators are certified
associative and commutative and their seeds match; idempotence is needed only
for duplicate elimination. It is not an arbitrary polyhedral-domain normalizer.
Unsupported partial-region combinations, symbolic extrema-bound queries, or user-supplied
inductive aggregate lemmas remain conservative. Certification has a one-second
deadline, at most 96 solver checks with 50 ms per check, and bounded tree walks.

For BenchExec, pass `--fold-operators docs/fold-operators.json` to
`tools/submit_benchexec.py` or `tools/run_benchexec.py`. The runner snapshots and
hashes the registry in its output directory, and forwards it through systemd
and the tool adapter to each solver worker.

The existing sum/product solver remains enabled and unchanged in its selection
priority; the new registry is opt-in. The original pivot algorithm is untouched.
