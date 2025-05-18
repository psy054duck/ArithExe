extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *) __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));
void reach_error() { __assert_fail("0", "MultCommutative-2.c", 3, "reach_error"); }

/*
 * Recursive implementation multiplication by repeated addition
 * Check that this multiplication is commutative
 * 
 * Author: Jan Leike
 * Date: 2013-07-17
 * 
 */

void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR:
        {reach_error();}
    }
    return;
}

extern int __VERIFIER_nondet_int(void);

// Multiplies two integers n and m
int add(int n, int m) {
    if (m == 0) {
        return n;
    }
    if (m > 0) {
        return add(n + 1, m - 1);
    }
    if (m < 0) {
        return add(n - 1, m + 1);
    }
}

int main() {
    int m = __VERIFIER_nondet_int();
    // if (m < 0 || m > 46340) {
    //     return 0;
    // }
    int n = __VERIFIER_nondet_int();
    // if (n < 0 || n > 46340) {
    //     return 0;
    // }
    int k = __VERIFIER_nondet_int();
    int res1 = add(m, add(n, k));
    int res2 = add(add(m, n), k);
    // 1 + 2 + ... + m = m*(m+1)/2
    // __VERIFIER_assert(res2 == m*(m+1)/2 || -res2 == -m*(-m + 1)/2);
    // __VERIFIER_assert(res3 == m + n + k);
    __VERIFIER_assert(res1 == res2);
}