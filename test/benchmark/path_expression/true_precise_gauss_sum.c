extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 1 || n > 1000) return 0;
    int sum = 0, i;
    for (i = 1; i <= n; ++i) sum += i;
    __VERIFIER_assert(i == n + 1 && 2 * sum == n * (n + 1));
    return 0;
}
