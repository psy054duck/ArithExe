extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void *malloc(unsigned long);

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int n;

int main(void) {
    n = __VERIFIER_nondet_int();
    if (n != 2) return 0;
    int *a = malloc(sizeof(int) * n);
    for (int i = 0; i < n; ++i) a[i] = __VERIFIER_nondet_int();
    __VERIFIER_assert(a[0] == a[1]);
    return 0;
}
