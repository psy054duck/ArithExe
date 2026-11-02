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
    assume_abort_if_not(n >= 2 && n <= 8);
    int a[n];
    for (int i = 0; i < n; ++i) {
        int k = __VERIFIER_nondet_int();
        if (i == 0) assume_abort_if_not(k > 0);
        a[i] = k;
    }
    __VERIFIER_assert(a[ASSERT_INDEX] > 0);
    return 0;
}
