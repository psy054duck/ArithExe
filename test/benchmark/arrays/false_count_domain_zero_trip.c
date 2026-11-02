extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int n = __VERIFIER_nondet_int();
    long long sum = 0;
    for (int i = 0; i < n; ++i) sum += 1;
    __VERIFIER_assert(sum == n);
    return 0;
}
