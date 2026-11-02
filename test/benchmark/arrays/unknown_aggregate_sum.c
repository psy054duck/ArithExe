extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 1 || n > 100) return 0;
    int a[100];
    for (int i = 0; i < n; ++i) a[i] = __VERIFIER_nondet_int();
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += a[i];
    __VERIFIER_assert(sum == 0);
    return 0;
}
