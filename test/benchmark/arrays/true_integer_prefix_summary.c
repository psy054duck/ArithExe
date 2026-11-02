extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);
void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 1 || n > 16) return 0;
    long long *a = malloc(sizeof(long long)*n);
    for (long long i = 0; i < n; ++i) {
        if (i == 0) a[0] = 2;
        else a[i] = a[i-1]+1;
    }
    for (long long i = 0; i < n; ++i)
        __VERIFIER_assert(a[i] == i+2);
    return 0;
}
