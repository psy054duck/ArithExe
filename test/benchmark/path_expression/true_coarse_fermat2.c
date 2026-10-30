// Fermat factorisation loop: correlated selects must yield only two maps.
extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int A = __VERIFIER_nondet_int();
    int R = __VERIFIER_nondet_int();
    if (!(((long long)R - 1) * ((long long)R - 1) < A)) return 0;
    if (!(A % 2 == 1)) return 0;
    long long u = 2LL * R + 1;
    long long v = 1;
    long long r = (long long)R * R - A;
    while (r != 0) {
        if (r > 0) {
            r -= v;
            v += 2;
        } else {
            r += u;
            u += 2;
        }
    }
    __VERIFIER_assert(4LL * A == u*u - v*v - 2*u + 2*v);
    return 0;
}
