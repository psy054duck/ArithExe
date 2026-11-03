# Native multidimensional VLA extraction

Checkpoint before this treatment: `7717f07619dda9f24953bdce71e5400fce88dd4c`
on `codex/native-multidimensional-arrays` (server repository).

Under integer semantics, the memory model preserves source dimensions for integral VLAs when LLVM
debug records provide a usable shape. Debug metadata is a proposal, not a
proof: each extent must be positive on the allocation path, and their product
must equal the LLVM allocation count. Dimension values are frozen at allocation.
Fixed multidimensional LLVM types retain their existing treatment.

The frontend reconstructs the separate coordinates of chained scalar GEPs by
exact algebraic extraction of stride multiples. For example, a row offset
`r * N` followed by a column offset `c` produces `(r, c)`, not a single
recurrence index `r * N + c`. The coordinate expressions preserve the
definedness of the original offset expressions. Array states and recurrences
therefore have one argument per dimension, plus the recurrence iteration.

At dereferences, inner coordinates must be certified canonical; otherwise
the tool reports unsupported semantics rather than silently changing aliasing.
Signed variable terms are extracted only as exact stride multiples; Euclidean
division of literal offsets must not split a reverse column such as `N-1-j`.
Checked carry/borrow adjustments preserve the linear address while restoring
canonical coordinates, without introducing symbolic division into recurrences.
The all-zero base address is canonical. Missing or uncertifiable shape metadata
leaves the original allocation representation unchanged.

In the native VLA path, child loops retain arithmetic constraints on frozen outer parameters, while
excluding facts about their changing state. Loop-count synthesis handles these
as parameter-domain assumptions, not additional loop guards. Stable scalar
load PHIs may retain their entry values only after a read-only memory-effect
check. Helper-local stack stores do not write caller memory.

The experimental flattened-row composition rule is removed in this treatment.
The original pivot recurrence transformation is unchanged. This frontend work
does not itself guarantee that every extracted scalar recurrence is solvable
by the current solver backend.
