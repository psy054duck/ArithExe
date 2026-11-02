extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void abort(void) __attribute__((noreturn));

void assume_abort_if_not(int condition) {
    if (!condition) abort();
}

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    int j = __VERIFIER_nondet_int();
    assume_abort_if_not(n >= 2 && n <= 8 && j > 0 && j < 10);
    int a[n];
    for (int i = 1; i < n; ++i) {
        int k = __VERIFIER_nondet_int();
        assume_abort_if_not(k >= MIN_K && k < 10);
        a[i] = i + j + k;
    }
    for (int i = 1; i < n; ++i) {
        __VERIFIER_assert(a[i] >= i + ASSERT_DELTA);
        __VERIFIER_assert(a[i] < i + j + 10);
    }
    return 0;
}
