extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int(), m = __VERIFIER_nondet_int();
    if (n < 0 || n > 10000 || m < -2 || m > 10000) return 0;
    int k = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) ++k;
    if (m > 0) __VERIFIER_assert(k == n * m);
    else __VERIFIER_assert(k == 0);
    return 0;
}
