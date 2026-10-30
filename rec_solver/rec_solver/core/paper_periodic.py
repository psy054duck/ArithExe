"""OOPSLA 2023 Algorithms 1--2 and Section 7.1 (no trace compression).

Algorithm 1 follows the prose specification: maximize suffix coverage, prefer
the shortest period, and extend backwards through a partial copy. Resource
exhaustion and solver unknown are failures, never evidence of periodicity.
"""
import z3


def generate(prefix):
    best = None
    for length in range(1, len(prefix) // 2 + 1):
        word = list(prefix[-length:])
        start = len(prefix) - length
        while start >= length and prefix[start-length:start] == word:
            start -= length
        coverage = len(prefix) - start
        if coverage >= 2 * length and (best is None or coverage > best[0]):
            best = coverage, start, word
    if best is None:
        return 0, []
    _, start, word = best
    shift = 0
    while start > 0 and prefix[start-1] == word[(-shift-1) % len(word)]:
        start -= 1
        shift += 1
    shift %= len(word)
    if shift:
        word = word[-shift:] + word[:-shift]
    return start, word


def symbolic_template(index_seq):
    """Section 7.1: longest repeated prefix S1, fixed S2, final S3."""
    finite = [item for word, count in index_seq[:-1]
              for _ in range(count) for item in word]
    tail = index_seq[-1][0]
    best = None
    for length in range(1, len(finite) // 2 + 1):
        word = finite[:length]
        count = 1
        while finite[count*length:(count+1)*length] == word:
            count += 1
        coverage = count * length
        if count >= 2 and (best is None or coverage > best[0]):
            best = coverage, word
    if best is None:
        return ([(finite, 1)] if finite else []) + [(tail, 1)], []
    coverage, word = best
    q = z3.Int('q0')
    result = [(word, q)]
    if finite[coverage:]:
        result.append((finite[coverage:], 1))
    return result + [(tail, 1)], [q]


def solve_initial(rec, bnd=100, enable_bounded_cfinite=True):
    from .ultimately_periodic import (_compute_solution_by_index_seq, verify,
                                     UnsolvableError)
    from .closed_form import PiecewiseClosedForm
    start, length = 0, 1
    values = rec.initial
    result = PiecewiseClosedForm(rec.ind_var)
    accepted = []
    while length * 2 <= bnd:
        length *= 2
        probe_values = {k.decl()(start): v for k, v in values.items()}
        _, trace = rec.get_n_values_starts_with(start, length, probe_values)
        threshold, tail = generate(trace)
        if not tail:
            continue
        pattern = ([(trace[:threshold], 1)] if threshold else []) + [(tail, 1)]
        local = rec.copy_rec_with_diff_initial(values)
        # Keep sequence applications local while shifting explicit time in guards.
        from .utils import get_applied_functions
        def shift_time(expr):
            apps = list(get_applied_functions(expr))
            masks = [z3.FreshConst(app.sort()) for app in apps]
            masked = z3.substitute(expr, *list(zip(apps, masks)))
            masked = z3.substitute(masked, (rec.ind_var, rec.ind_var + start))
            return z3.substitute(masked, *list(zip(masks, apps)))
        local._conditions = [shift_time(cond) for cond in local.conditions]
        candidate = _compute_solution_by_index_seq(
            local, pattern, enable_bounded_cfinite=enable_bounded_cfinite)
        violation = verify(local, candidate, pattern)
        result = result.concatenate(candidate.subs(
            {candidate.ind_var: candidate.ind_var - start}), start)
        if violation is None:
            accepted.extend(pattern)
            return result, accepted
        if violation <= threshold:
            raise UnsolvableError('Periodic candidate made no certified progress')
        if threshold:
            accepted.append((trace[:threshold], 1))
        copies, remainder = divmod(violation - threshold, len(tail))
        if copies:
            accepted.append((tail, copies))
        if remainder:
            accepted.append((tail[:remainder], 1))
        start += violation
        values = result.eval_at(start).as_dict()
        values = {k.decl()(0): v
                  for k, v in values.items()}
    raise UnsolvableError('OOPSLA 2023 sampling bound exhausted')
