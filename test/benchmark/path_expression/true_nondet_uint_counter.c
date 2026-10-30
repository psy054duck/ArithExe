extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);

int main(void) {
    unsigned int i = 0, j = 0;
    while (__VERIFIER_nondet_uint()) { ++i; j += 2; }
    __VERIFIER_assert(j == 2 * i);
    return 0;
}
