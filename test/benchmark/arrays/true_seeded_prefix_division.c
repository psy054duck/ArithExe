extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);
void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 1 || n > 64) return 0;
    long long *a = malloc(sizeof(long long)*n);
    long long *b = malloc(sizeof(long long)*n);
    for (int i = 0; i < n; ++i) {
        if (i == 0) a[0] = 2;
        else a[i] = a[i-1]+1;
    }
    for (int i = 0; i < n; ++i) {
        if (i == 0) b[0] = 0;
        else b[i] = b[i-1]+a[i-1];
    }
    for (int i = 0; i < n; ++i)
        __VERIFIER_assert(b[i] == (1LL*i*(i+3))/2);
    return 0;
}
