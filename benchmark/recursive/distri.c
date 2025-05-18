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

int addition(int m, int n) {
    if (n == 0) {
        return m;
    }
    if (n > 0) {
        return addition(m+1, n-1);
    }
    return addition(m-1, n+1);
}

int mult(int n, int m) {
    if (m < 0) {
        return -mult(n, -m);
    }
    if (m == 0) {
        return 0;
    }
    return n + mult(n, m - 1);
}

int main() {
    int n = __VERIFIER_nondet_int();
    int j = __VERIFIER_nondet_int();
    int k = __VERIFIER_nondet_int();
    int res1 = mult(n, addition(j, k));
    int res2 = addition(mult(n, j), mult(n, k));
    __VERIFIER_assert(res1 == res2);
}