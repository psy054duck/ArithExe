extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 2 || n > 1000) return 0;
    int sum = 0;
    for (int i = 1; i <= n; ++i) sum += i;
    __VERIFIER_assert(2 * sum == n * (n + 1) + 2);
    return 0;
}
