extern int __VERIFIER_nondet_int(void);
extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
extern void reach_error(void);

int safe_suffix(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0) return 0;
    __VERIFIER_assert(n >= 0);
    unsigned int x = 0;
    while (__VERIFIER_nondet_bool()) x = x * x + 1;
    return 0;
}

int unsafe_pending_assert(void) {
    __VERIFIER_assert(0);
    while (__VERIFIER_nondet_bool()) {}
    return 0;
}

int unsafe_pending_error(void) {
    reach_error();
    while (__VERIFIER_nondet_bool()) {}
    return 0;
}

int unsafe_sibling(void) {
    if (__VERIFIER_nondet_bool()) reach_error();
    else while (__VERIFIER_nondet_bool()) {}
    return 0;
}

__attribute__((noinline)) int safe_value(void) {
    return __VERIFIER_nondet_int();
}

__attribute__((noinline)) int middle_value(void) {
    return safe_value();
}

int unsafe_caller(void) {
    int x = safe_value();
    __VERIFIER_assert(x == 0);
    return 0;
}

int unsafe_outer_caller(void) {
    int x = middle_value();
    __VERIFIER_assert(x == 0);
    return 0;
}

__attribute__((noinline)) void failing_helper(void) {
    reach_error();
}

int unsafe_callee(void) {
    failing_helper();
    return 0;
}

__attribute__((noinline)) void checked_helper(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0) return;
    __VERIFIER_assert(n >= 0);
    while (__VERIFIER_nondet_bool()) {}
}

int safe_callee_suffix(void) {
    checked_helper();
    return 0;
}

int dead_error(void) {
    int n = __VERIFIER_nondet_int();
    if (n > 0) {
        if (!(n > 0)) reach_error();
    }
    while (__VERIFIER_nondet_bool()) {}
    return 0;
}

int main(void) {
    while (__VERIFIER_nondet_bool()) {}
    return 0;
}
