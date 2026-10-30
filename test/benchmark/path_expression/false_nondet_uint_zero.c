extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 0;
    while (__VERIFIER_nondet_uint()) ++x;
    __VERIFIER_assert(x != 0);
    return 0;
}
