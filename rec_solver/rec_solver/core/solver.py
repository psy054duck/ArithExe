import z3

from ..rec_parser import parse_str
from .recurrence import LoopRecurrence, MultiRecurrence
from .multivariate import solve_multivariate_rec
from .ultimately_periodic import solve_ultimately_periodic_symbolic
from .poly_expr import poly_expr_solving
from .bounded_cfinite import can_attempt_bounded_cfinite_map, solve_bounded_cfinite_map
from .solvable_polynomial import is_solvable_map
from .utils import conversion_cache
from .modulo_normalization import DEFAULT_MAX_BRANCHES, modulo_branch_limit, normalize_constant_modulo
from .phase_lengths import phase_length_scope
from .verification import is_inductive_scalar_closed_form as _direct_function_closed_form_is_inductive


def _closed_form_has_content(closed_form):
    if closed_form is None:
        return False
    try:
        closed_dict = closed_form.as_dict()
    except AttributeError:
        return True
    if not closed_dict:
        return False
    return any(
        not z3.is_true(z3.simplify(lhs == rhs))
        for lhs, rhs in closed_dict.items()
    )


def solve_str(
    s,
    enable_bounded_cfinite=True,
    poly_expr_strategy="auto",
    poly_expr_order=1,
    poly_expr_degree=None,
    multivariate_strategy="pivot",
    tupling_options=None,
    modulo_max_branches=DEFAULT_MAX_BRANCHES,
    phase_length_options=None,
    phase_length_stats=None,
    enable_aggregates=True,
):
    with conversion_cache(), modulo_branch_limit(modulo_max_branches), \
            phase_length_scope(phase_length_options, phase_length_stats):
        return _solve_str(s, enable_bounded_cfinite, poly_expr_strategy,
                          poly_expr_order, poly_expr_degree,
                          multivariate_strategy, tupling_options, modulo_max_branches,
                          enable_aggregates)


def _solve_str(s, enable_bounded_cfinite, poly_expr_strategy, poly_expr_order,
               poly_expr_degree, multivariate_strategy, tupling_options, modulo_max_branches,
               enable_aggregates=True):
    if multivariate_strategy not in {"pivot", "tupling"}:
        raise ValueError("multivariate_strategy must be 'pivot' or 'tupling'")
    rec = parse_str(s)
    if isinstance(rec, LoopRecurrence):
        from .periodic_counters import solve_periodic_counter
        counter = solve_periodic_counter(rec)
        if counter is not None:
            return counter
        rec = normalize_constant_modulo(rec, max_branches=modulo_max_branches)
        from .aggregates import has_external_inputs, solve_aggregate_recurrence
        # Pure arithmetic triangular sums need the same early exact path as
        # entry-array sums; otherwise symbolic periodic solving may dominate.
        if enable_aggregates:
            aggregate = solve_aggregate_recurrence(rec)
            # Residual arithmetic aggregates are exact representations, but
            # cannot replace the closed scalar forms used by loop guards.
            # Preserve the previous solver priority unless arithmetic closed.
            if aggregate is not None and (has_external_inputs(rec) or
                                          not aggregate.aggregates.definitions):
                return aggregate
        solvable = False
        try:
            solvable = is_solvable_map(rec)
        except:
            pass
        if enable_bounded_cfinite and not solvable and can_attempt_bounded_cfinite_map(rec, require_nonlinear=True):
            try:
                res = solve_bounded_cfinite_map(rec)
                if (_closed_form_has_content(res) and
                        _direct_function_closed_form_is_inductive(rec, res)):
                    return res
            except:
                pass
        try:
            res = solve_ultimately_periodic_symbolic(
                rec,
                enable_bounded_cfinite=enable_bounded_cfinite,
                modulo_max_branches=modulo_max_branches,
            )
            if _closed_form_has_content(res):
                return res
        except:
            pass
        if enable_bounded_cfinite:
            try:
                res = solve_bounded_cfinite_map(rec)
                if (_closed_form_has_content(res) and
                        _direct_function_closed_form_is_inductive(rec, res)):
                    return res
            except:
                pass
        if enable_aggregates:
            aggregate = solve_aggregate_recurrence(rec)
            if aggregate is not None:
                return aggregate
        degrees = [poly_expr_degree] if poly_expr_degree is not None else [2, 3]
        res = None
        for degree in degrees:
            res = poly_expr_solving(
                rec,
                degree,
                strategy=poly_expr_strategy,
                order=poly_expr_order,
            )
            if _closed_form_has_content(res):
                return res
        return res
    elif isinstance(rec, MultiRecurrence):
        if multivariate_strategy == "tupling":
            from .tupling import solve_tupled_rec
            return solve_tupled_rec(rec, options=tupling_options)
        return solve_multivariate_rec(rec)
    return None

def solve_file(
    filename,
    enable_bounded_cfinite=True,
    poly_expr_strategy="auto",
    poly_expr_order=1,
    poly_expr_degree=None,
    multivariate_strategy="pivot",
    tupling_options=None,
    modulo_max_branches=DEFAULT_MAX_BRANCHES,
    phase_length_options=None,
    phase_length_stats=None,
    enable_aggregates=True,
):
    with open(filename) as fp:
        return solve_str(
            fp.read(),
            enable_bounded_cfinite=enable_bounded_cfinite,
            poly_expr_strategy=poly_expr_strategy,
            poly_expr_order=poly_expr_order,
            poly_expr_degree=poly_expr_degree,
            multivariate_strategy=multivariate_strategy,
            tupling_options=tupling_options,
            modulo_max_branches=modulo_max_branches,
            phase_length_options=phase_length_options,
            phase_length_stats=phase_length_stats,
            enable_aggregates=enable_aggregates,
        )
