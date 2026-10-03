extern unsigned char __VERIFIER_nondet_uchar(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int sum = 0;
    for (unsigned int i = 0; i < 3; ++i)
        sum += __VERIFIER_nondet_uchar();
    __VERIFIER_assert(sum <= 3 * 255);
    return 0;
}
