extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 1 || n > 16) return 0;
    long long *a = malloc(sizeof(long long) * n);
    long long *b = malloc(sizeof(long long) * n);
    a[0] = 2;
    b[0] = 1;
    for (int i = 1; i < n; ++i) a[i] = a[i-1] + 20;
    for (int i = 1; i < n; ++i) b[i] = b[i-1] + a[i-1];
    for (int i = 0; i < n; ++i) {
        if (i == 0) __VERIFIER_assert(b[i] == 1);
        __VERIFIER_assert(b[i] == 10LL*i*i - 8LL*i + 1);
    }
    return 0;
}
