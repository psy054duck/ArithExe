#include "assert.h"

int __VERIFIER_nondet_int(void);
void __VERIFIER_assume(int);

// Ground truth: UNSAFE. The error is reachable when n == 2.
// Expected verifier result before Z3 can decide the branch: UNKNOWN, not HOLD.

int main(void) {
    int n = __VERIFIER_nondet_int();
    __VERIFIER_assume(n >= 0);

    int value = 1;
    for (int i = 0; i < n; ++i) {
        value *= 2;
    }

    if (value == 4) {
        assert(0);
    }

    return 0;
}
