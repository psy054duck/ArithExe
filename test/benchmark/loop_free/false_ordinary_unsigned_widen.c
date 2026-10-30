extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int z = __VERIFIER_nondet_uint();
    __VERIFIER_assume(z == 0);
    unsigned long long x = 1, y = z;
    x *= z - 1;
    __VERIFIER_assert(1 + x - y == 0);
    return 0;
}
