extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));

int sum(int n, int a) {
    if (n <= 0) return a;
    return sum(n - 1, a) + n;
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    int a = __VERIFIER_nondet_int();
    if (n < 0 || n > 100 || a < 0 || a > 100) return 0;
    if (sum(n, a) != a + n * (n + 1) / 2) __VERIFIER_error();
    return 0;
}
