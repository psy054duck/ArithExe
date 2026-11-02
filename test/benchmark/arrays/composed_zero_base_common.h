extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
extern void *malloc(unsigned int);

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n <= 0 || n > 64) return 0;
    long long *a = malloc(sizeof(long long)*n);
    long long *b = malloc(sizeof(long long)*n);
    long long *c = malloc(sizeof(long long)*n);
    for (int i = 0; i < n; ++i) {
        if (i == 0) a[0] = 0;
        else a[i] = a[i-1]+6;
    }
    for (int i = 0; i < n; ++i) {
        if (i == 0) b[0] = 1;
        else b[i] = b[i-1]+a[i-1];
    }
    for (int i = 0; i < n; ++i) {
        if (i == 0) c[0] = 0;
        else c[i] = c[i-1]+b[i-1];
    }
    for (int i = 0; i < n; ++i)
        __VERIFIER_assert(c[i] == EXPECTED_C(i));
    return 0;
}
