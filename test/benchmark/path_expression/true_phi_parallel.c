extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0 || n > 2) return 0;
    int p = 1, q = 2;
    for (int i = 0; i < n; ++i) {
        int old_p = p;
        p = q;
        q = old_p;
    }
    __VERIFIER_assert(p == (n == 1 ? 2 : 1));
    __VERIFIER_assert(q == (n == 1 ? 1 : 2));
    return 0;
}
