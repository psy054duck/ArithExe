extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int n;

int main(void) {
    n = __VERIFIER_nondet_int();
    if (n < 1 || n > 8) return 0;
    int *a = malloc(sizeof(int) * n);
    int i;
    for (i = 0; i < n; ++i) a[i] = __VERIFIER_nondet_int();
    for (i = 0; i < n; ++i) {
        if (a[i] > n) a[i] = a[i];
        else a[i] = n;
    }
    for (i = 0; i < n; ++i) __VERIFIER_assert(a[i] <= n);
    return 0;
}
