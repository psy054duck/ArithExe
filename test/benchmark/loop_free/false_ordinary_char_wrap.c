extern unsigned char __VERIFIER_nondet_uchar(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned char c = __VERIFIER_nondet_uchar();
    __VERIFIER_assume(c == 255);
    unsigned char sum = c;
    sum += c;
    __VERIFIER_assert(sum >= c);
    return 0;
}
