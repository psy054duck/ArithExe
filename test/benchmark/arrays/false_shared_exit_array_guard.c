extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int condition) { if (!condition) __VERIFIER_error(); }

int main(void) {
    volatile int a[1]; /* Keep the accesses through scalar replacement. */
    a[0] = 0;
    int i = 0;
    while (a[0] == 0) {
        if (__VERIFIER_nondet_int()) break;
        a[0] = 1;
        ++i;
    }
    __VERIFIER_assert(i == 2);
    return 0;
}
