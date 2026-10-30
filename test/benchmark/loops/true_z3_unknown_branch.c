extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    __VERIFIER_assume(n >= 0);
    int value = 1;
    for (int i = 0; i < n; ++i) value *= 2;
    if (value == 4) __VERIFIER_assert(value > 0);
    return 0;
}
