extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 1 || n > 16) return 0;
    long long *a = malloc(sizeof(long long) * n);
    long long sum = 0;
    for (int i = 0; i < n; ++i) a[i] = 1;
    for (int i = 0; i < n; ++i) sum += a[i];
    for (int i = 0; i < n; ++i) a[i] += sum;
    for (int i = 0; i < n; ++i) a[i] %= sum + 1;
    for (int i = 0; i < n; ++i)
        if (a[i] != 1) __VERIFIER_error();
    return 0;
}
