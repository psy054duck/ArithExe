extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int condition) { if (!condition) __VERIFIER_error(); }

int main(void) {
    int a[8];
    int i, sum = 0;
    for (i = 0; i < 8; ++i) {
        if (__VERIFIER_nondet_int()) break;
        a[i] = sum;
        sum += i;
    }
    __VERIFIER_assert(i >= 0 && i <= 8);
    __VERIFIER_assert(2*sum == i*(i-1));
    for (int k = 1; k < i; ++k) __VERIFIER_assert(a[k] >= 0);
    return 0;
}
