extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int m = __VERIFIER_nondet_int();
    if (m < -2 || m > 10000) return 0;
    int k = 0;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < m; ++j) ++k;
    __VERIFIER_assert(k >= 100);
    return 0;
}
