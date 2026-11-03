# Piecewise Polynomial Sums

The early aggregate path evaluates supported arithmetic sums before the general
periodic solver. It handles polynomial bodies through degree four, nested ITEs,
and Boolean combinations of comparisons whose index coefficient is +1 or -1.
All other terms must be stable parameters. Unsupported functions, nonexact
integer quotients, scaled-index guards, modular guards, or budget exhaustion
fall back to existing solvers or residual aggregates.

The finite-sum kernel clips half-open integer intervals and partitions Boolean
unions disjointly. Polynomial prefix expressions are checked by exact symbolic
difference and base identities. Their integer values are certified through
integer coefficients in the multivariate Newton basis. No floating-point
coefficients or assumed bounds on parameters are used. Empty intervals return
zero, and negative interval endpoints are supported.

Final proof queries can add bounded exact-division lemmas. For an integer
polynomial P and a literal divisor d between 2 and 8, exhaustive evaluation of
all residue assignments modulo d certifies whether P is always divisible by d.
Only then is d*(P div d)=P added. Work is bounded by four variables, 256 nodes
per candidate, 30000 evaluation steps, and a 100 ms query deadline. Arithmetic
sum-of-monomials normalization runs only when such a lemma was obtained.

Patterns 27--30 are integration examples, not recognized source patterns.
Safe and changed-specification matrix fixtures cover row sums, antisymmetry,
column sums, and distances. The original alternating-value pattern 26 is not
claimed solved by this change.

The alpha-zero recurrence case separately preserves its initial value at zero
iterations. Aggregate metadata explicitly keeps the function application on the
left of its defining equality, including constant bodies.
