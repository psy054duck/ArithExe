extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
extern void __VERIFIER_assume(int);
int main(void) {
    int A = __VERIFIER_nondet_int(), R = __VERIFIER_nondet_int();
    __VERIFIER_assume(((long long)R - 1) * ((long long)R - 1) < A);
    __VERIFIER_assume(A % 2 == 1);
    long long u = 2LL * R + 1, v = 1, r = (long long)R * R - A;
    while (r != 0) {
        while (r > 0) { r -= v; v += 2; }
        while (r < 0) { r += u; u += 2; }
    }
    __VERIFIER_assert(4LL * A == u*u - v*v - 2*u + 2*v);
}
