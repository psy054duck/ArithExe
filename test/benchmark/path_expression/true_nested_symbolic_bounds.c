extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int(), m = __VERIFIER_nondet_int();
    if (n < 10 || n > 10000 || m < 10 || m > 10000) return 0;
    int k = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) ++k;
    __VERIFIER_assert(k >= 100);
    return 0;
}
