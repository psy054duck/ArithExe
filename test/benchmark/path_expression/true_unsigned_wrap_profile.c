extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);
extern void __VERIFIER_assume(int);
int main(void) {
    unsigned int x = __VERIFIER_nondet_uint();
    __VERIFIER_assume(x == 0);
    x--;
    __VERIFIER_assert(x > 10);
    return 0;
}
