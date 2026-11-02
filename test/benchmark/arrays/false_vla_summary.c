extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n <= 0 || n > 64) return 0;
    int a[n];
    for (int i = 0; i < n; ++i) a[i] = i+5;
    for (int i = 0; i < n; ++i) __VERIFIER_assert(a[i] == i+6);
    return 0;
}
