extern int __VERIFIER_nondet_int(void);
extern void abort(void) __attribute__((noreturn));
extern void __VERIFIER_error(void) __attribute__((noreturn));

int main(void) {
    int N = __VERIFIER_nondet_int();
    if (N < 2) abort();
    int a[N];
    a[0] = 0;
    a[1] = 1;
    for (int i = 2; i < N; ++i) {
        if (i % 2 == 0)
            a[i] = 2 * a[i-1] - a[i-2];
        else
            a[i] = 3 * a[i-1] - 2 * a[i-2] - 1;
    }
    for (int i = 0; i < N; ++i)
        if (a[i] != i || (EXPECTED_SHIFT && i == 17)) __VERIFIER_error();
    return 0;
}
