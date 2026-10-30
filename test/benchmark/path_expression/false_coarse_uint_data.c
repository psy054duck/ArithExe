extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);

int main(void) {
    unsigned int x = 0;
    for (int i = 0; i < 1; ++i) {
        unsigned int value = __VERIFIER_nondet_uint();
        if (value) x = value;
    }
    __VERIFIER_assert(x <= 1);
    return 0;
}
