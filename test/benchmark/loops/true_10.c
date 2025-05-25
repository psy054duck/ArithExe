// Source: Sumit Gulwani, Saurabh Srivastava, Ramarathnam Venkatesan: "Program
// Analysis as Constraint Solving", PLDI 2008.

#include "assert.h"
int N = 50;
int n = 0;
int main() {
    int x,y;
    x = __VERIFIER_nondet_int();
    int x0 = x;
    while (n++ < N) {
	x++;
    }
    __VERIFIER_assert(x == x0 + 50);
    return 0;
}

