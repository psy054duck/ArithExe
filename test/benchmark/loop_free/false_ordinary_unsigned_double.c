extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int b = __VERIFIER_nondet_uint();
    __VERIFIER_assume(b == 3200000002U);
    unsigned int doubled = 2U * b;
    unsigned int half = doubled / 2U;
    __VERIFIER_assert(half == b);
    return 0;
}
