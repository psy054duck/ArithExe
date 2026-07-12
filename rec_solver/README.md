# Conditional Recurrence Solver
## Overview

This is a recurrence solver based on our following works:

- On Polynomial Expressions with C-Finite Recurrences in Loops with Nested Nondeterministic Branches (CAV 2024)
- Solving Conditional Linear Recurrences for Program Verification: The Periodic Case (OOPSLA 2023)

Besides the aforementioned works,
the solver now supports computing closed-form solutions to multivariate functions.

## Polynomial-expression strategies

The `arith_exe` wrapper accepts
`--poly-expr-strategy=auto|special|algorithm2|general`.
The `special` strategy keeps the previous polynomial-expression procedure,
`algorithm2` runs the spectral-intersection algorithm for nested
nondeterministic branches, and `general` runs the SMT-backed bounded-degree,
bounded-order search. The default `auto` mode first tries the previous
procedure, then Algorithm 2, and falls back to the general search when needed.
Use `--poly-expr-order=N` to set the order `N` of the C-finite recurrence and
`--poly-expr-degree=N` to set the polynomial degree bound. If the degree is not
provided, the solver keeps the previous fallback behavior of trying degree 2 and
then degree 3.

## Standalone polynomial-expression experiments

Polynomial-expression synthesis can also be run directly, without invoking the
`arith_exe` C++ frontend. This is useful for small examples, batch experiments,
and checking the polynomial spaces returned by each strategy.

Activate the Python environment first:

```sh
conda activate veri
```

From this directory, run:

```sh
python poly_expr_experiment.py \
  --vars x,y \
  --transition 'x=2*x+1; y=4*y-4*x+2' \
  --transition 'x=2*x; y=4*y+3' \
  --degree 2 \
  --order 1 \
  --strategy algorithm2
```

Each `--transition` argument is one nondeterministic branch. Assignments may be
separated by semicolons, and variables omitted from a branch are treated as
identity updates. The script prints each polynomial-expression space, its basis,
the recurrence satisfied by that space, the characteristic polynomial, and, when
possible, a closed form for each basis element. Initial values default to
`x0`, `y0`, and so on; use `--initial 'x=1; y=2'` to override them.

For the running nested-branch example, one can run:

```sh
python poly_expr_experiment.py \
  --vars x,y,z \
  --transition 'z=1-z; x=2*x+y^2+z; y=2*y-y^2+2*z' \
  --initial 'z=0' \
  --degree 1 \
  --order 3 \
  --strategy algorithm2
```

Use `--json` for machine-readable output and `--no-closed-forms` when only the
basis and recurrences are needed:

```sh
python poly_expr_experiment.py \
  --vars x,y,z \
  --transition 'z=1-z; x=2*x+y^2+z; y=2*y-y^2+2*z' \
  --initial 'z=0' \
  --degree 1 \
  --order 3 \
  --strategy algorithm2 \
  --json \
  --no-closed-forms
```

The same information can be supplied through JSON:

```json
{
  "variables": ["x", "y", "z"],
  "transitions": [
    "z=1-z; x=2*x+y^2+z; y=2*y-y^2+2*z"
  ],
  "initial": "z=0",
  "degree": 1,
  "order": 3,
  "strategy": "algorithm2"
}
```

Then run:

```sh
python poly_expr_experiment.py --input-json problem.json --json
```

For Python experiments, import the extracted solver API directly:

```python
import sympy as sp
from rec_solver import find_cfinite_spaces, solve_cfinite_rec

x, y = sp.symbols("x y", integer=True)
n = sp.Symbol("n", integer=True)
transitions = [
    {x: 2*x + 1, y: 4*y - 4*x + 2},
    {x: 2*x, y: 4*y + 3},
]
initial = {x: sp.Symbol("x0", integer=True), y: sp.Symbol("y0", integer=True)}

spaces = find_cfinite_spaces(
    [x, y],
    transitions,
    n,
    d=2,
    order=1,
    strategy="algorithm2",
)
for space in spaces:
    print(space.recurrence_coeffs, space.basis_instances)
    for expr in space.basis_instances:
        print(solve_cfinite_rec(space.recurrence_coeffs, expr, transitions, initial, [x, y], n))
```
