from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import z3

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'rec_solver'))

from rec_solver.rec_parser import parse_str
from rec_solver.core import logic_simplification as logic
from rec_solver.core import utils
from rec_solver.core.solvable_polynomial import _parameter_integer_divisions


class DagPreprocessingTests(unittest.TestCase):
    def test_division_free_pass_does_not_collect_or_render_variable_names(self):
        rec = parse_str('i(0)=0; s(0)=p; if (true) { i(n+1)=i(n)+1; s(n+1)=s(n)+6*i(n); }')
        with patch.object(utils, 'get_vars', side_effect=AssertionError('unnecessary names')), \
                patch.object(utils, 'get_func_decls', side_effect=AssertionError('unnecessary functions')):
            self.assertEqual(_parameter_integer_divisions(rec), [])

    def test_literal_division_does_not_collect_parameter_names(self):
        rec = parse_str('i(0)=0; if (true) { i(n+1)=i(n)+(3/2); }')
        with patch.object(utils, 'get_vars', side_effect=AssertionError('numeric quotient')):
            self.assertEqual(_parameter_integer_divisions(rec), [])

    def test_shared_dag_is_visited_once_by_both_variable_apis(self):
        x = z3.Int('dag_x')
        term = x
        for _ in range(24):
            term = term+term
        original = z3.ExprRef.children
        for collect in (utils.get_vars, logic.get_vars):
            visits = []
            def children(node):
                visits.append(node.get_id())
                if len(visits)>30:
                    raise AssertionError('shared DAG expanded as a tree')
                return original(node)
            with patch.object(z3.ExprRef, 'children', children):
                self.assertEqual(collect(term), {x})
            self.assertEqual(len(visits), len(set(visits)))

    def test_shared_quotient_produces_one_parameter(self):
        rec = parse_str('i(0)=0; s(0)=u; if (true) { i(n+1)=i(n)+1; s(n+1)=s(n)+p/2+p/2; }')
        self.assertEqual(len(_parameter_integer_divisions(rec)), 1)

    def test_quantified_bound_variables_are_not_free_inputs(self):
        x, y = z3.Ints('dag_bound_x dag_free_y')
        for collect in (utils.get_vars, logic.get_vars):
            self.assertEqual(collect(z3.ForAll(x, x>=y)), {y})

    def test_ast_ids_are_not_cached_across_contexts(self):
        left, right = z3.Context(), z3.Context()
        x, y = z3.Int('dag_same', ctx=left), z3.Int('dag_same', ctx=right)
        self.assertEqual(utils.get_vars(x+x), {x})
        self.assertEqual(utils.get_vars(y+y), {y})

    def test_deep_expression_does_not_use_python_recursion(self):
        x = z3.Int('deep_dag_x')
        term = x
        for _ in range(1200):
            term = term+1
        self.assertEqual(utils.get_vars(term), {x})


if __name__ == '__main__':
    unittest.main()
