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
        if (n % 2 == 0) {
            x++;
        } else {
            x += 2;
        }
    }
    while (n++ < 2*N) {
        x++;
    }
    __VERIFIER_assert(x == x0 + N/2*3 + N - 1);
    return 0;
}

