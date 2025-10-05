extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *) __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));
void reach_error() { __assert_fail("0", "Fibonacci01-1.c", 3, "reach_error"); }

/*
 * Recursive computation of fibonacci numbers.
 * 
 * Author: Matthias Heizmann
 * Date: 2013-07-13
 * 
 */

extern int __VERIFIER_nondet_int(void);
void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR:
        {reach_error();}
    }
    return;
}

int g(int n) {
    if (n < 2) {
        return n;
    } else {
        int x1 = 2*g(n - 1);
        int x2 = g(n - 2);
        return x1 - x2;
    }
}

int f(int n) {
    if (n < 2) {
        return g(n);
        // return n + 1;
    } else {
        int x1 = 2*f(n - 1);
        int x2 = f(n - 2);
        return x1 - x2;
    }
}

int main() {
    int x = __VERIFIER_nondet_int();
    int result = f(x);
    __VERIFIER_assert(result == x);
}
    
