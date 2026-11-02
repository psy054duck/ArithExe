extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));

int progression(int n, int a) {
    if (n <= 1) return a + n;
    return 2 * progression(n - 1, a) - progression(n - 2, a);
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    int a = __VERIFIER_nondet_int();
    if (n < 0 || n > 50 || a < 0 || a > 100) return 0;
    if (progression(n, a) != a + n) __VERIFIER_error();
    return 0;
}
