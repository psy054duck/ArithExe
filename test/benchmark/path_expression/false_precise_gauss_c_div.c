extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 2 || n > 10) return 0;
    int sum = 0;
    for (int i = 1; i <= n; ++i) sum += i;
    // n=4 gives sum=10 and sum/3=3, not the rational value 10/3.
    __VERIFIER_assert(6 * (sum / 3) == n * (n + 1));
    return 0;
}
