extern unsigned int __VERIFIER_nondet_uint(void);
extern unsigned char __VERIFIER_nondet_uchar(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

static long long signed_argument(int value) {
    return value;
}

static unsigned int wrapped_result(unsigned int value) {
    return value + 1U;
}

int main(void) {
    unsigned int x = __VERIFIER_nondet_uint();
    __VERIFIER_assume(x == 4294967295U);
    __VERIFIER_assert(x + 1U == 0U);
    __VERIFIER_assert(x * 2U == 4294967294U);
    __VERIFIER_assert((x >> 31) == 1U);
    __VERIFIER_assert((x & 255U) == 255U);
    __VERIFIER_assert(signed_argument((int)x) == -1LL);
    __VERIFIER_assert(wrapped_result(x) == 0U);
    unsigned char c = __VERIFIER_nondet_uchar();
    __VERIFIER_assume(c == 255);
    c += 255;
    __VERIFIER_assert(c == 254);
    unsigned int z = __VERIFIER_nondet_uint();
    __VERIFIER_assume(z == 0);
    unsigned long long wide = z - 1;
    __VERIFIER_assert(wide == 4294967295ULL);
    return 0;
}
