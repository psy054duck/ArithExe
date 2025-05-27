// Source: Sumit Gulwani, Saurabh Srivastava, Ramarathnam Venkatesan: "Program
// Analysis as Constraint Solving", PLDI 2008.

#include "assert.h"
int main() {
    int x,y;
    x = __VERIFIER_nondet_int();
    y = __VERIFIER_nondet_int();
    int x0 = x;
    while (x < 50) {
        x = x + 1;
    }
    __VERIFIER_assert(x >= 50);
    return 0;
}

