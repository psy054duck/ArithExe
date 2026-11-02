import unittest
import z3

from rec_solver.core.phase_lengths import solve_integer_phase_length


class IntegerPhaseLengths(unittest.TestCase):
    def check_solution(self, formula, variable):
        result = solve_integer_phase_length(formula, variable)
        self.assertIsNotNone(result)
        regions = []
        for region, mapping in result:
            solver = z3.Solver()
            solver.add(region, z3.Not(z3.substitute(formula, list(mapping.items()))))
            self.assertEqual(solver.check(), z3.unsat)
            regions.append(region)
        solver = z3.Solver()
        solver.add(formula, z3.Not(z3.Or(*regions)))
        self.assertEqual(solver.check(), z3.unsat)
        return result

    def test_floor_count_for_flattened_rows(self):
        x, n, k = z3.Ints('x n k')
        self.check_solution(z3.And(x >= 1, 5*x <= 5*n-k, 5*n-k < 5*(x+1)), x)

    def test_ceiling_and_negative_parameters(self):
        x, n = z3.Ints('x n')
        self.check_solution(z3.And(5*x >= n, 5*(x-1) < n), x)

    def test_nonunique_integer_interval(self):
        x, n = z3.Ints('x n')
        self.check_solution(z3.And(x >= n, x >= 1, x <= n+10), x)

    def test_disjoint_regions(self):
        x, n = z3.Ints('x n')
        self.check_solution(z3.Or(z3.And(n >= 0, x >= 0, 3*x <= n, n < 3*(x+1)),
                                 z3.And(n < 0, x == 1)), x)

    def test_rejects_unhandled_holes_in_interval(self):
        x, n = z3.Ints('x n')
        self.assertIsNone(solve_integer_phase_length(z3.And(x >= n, x <= n+1, x % 2 == 0), x))

    def test_rejects_nonlinear_and_real_counts(self):
        x, n = z3.Ints('x n')
        self.assertIsNone(solve_integer_phase_length(x*x == n, x))
        r = z3.Real('r')
        self.assertIsNone(solve_integer_phase_length(r >= 0, r))


if __name__ == '__main__':
    unittest.main()
