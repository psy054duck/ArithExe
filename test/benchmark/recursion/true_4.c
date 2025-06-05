#include "assert.h"
int ackermann(int m, int n) {
    if (m==0) {
        return n+1;
    }
    if (n==0) {
        return ackermann(m-1,1);
    }
    return ackermann(m-1,ackermann(m,n-1));
}


int main() {
    int m = __VERIFIER_nondet_int();
    if (m < 0 || m > 3) {
        // additional branch to avoid undefined behavior 
        // (because of signed integer overflow)
        return 0;
    }
    int n = __VERIFIER_nondet_int();
    if (n < 0 || n > 23) {
        // additional branch to avoid undefined behavior 
        // (because of signed integer overflow)
        // 
        return 0;
    }
    int result = ackermann(m,n);
    __VERIFIER_assert(m < 0 || n < 0 || result >= 0);
}
