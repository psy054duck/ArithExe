extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int n;

int main(void) {
    n = __VERIFIER_nondet_int();
    if (n < 1 || n > 16) return 0;
    int *a = malloc(sizeof(int) * n);
    int i;
    for (i = 0; i < n; ++i) a[i] = 0;
    for (i = 0; i < n; ++i) {
        if (n % 2 == 0) a[i] = a[i] + 2;
        else a[i] = a[i] + 1;
    }
    for (i = 0; i < n; ++i) __VERIFIER_assert(a[i] % 2 == n % 2);
    return 0;
}
