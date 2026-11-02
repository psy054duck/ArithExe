extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n <= 0) return 0;
    int a[n];
    long long sum = 0;
    for (int i = 0; i < n; ++i) a[i] = 1;
    for (int i = 0; i < n; ++i) sum += a[i];
    for (int i = 0; i < n; ++i) sum -= a[i];
    __VERIFIER_assert(sum == EXPECTED_SUM);
    return 0;
}
