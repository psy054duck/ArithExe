extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *) __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));
void reach_error() { __assert_fail("0", "test5.c", 3, "reach_error"); }

extern int __VERIFIER_nondet_int(void);
void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR:
        {reach_error();}
    }
    return;
}

int f(int n) {
    if (n < 3) {
        return n*n;
    } else {
        int x1 = 2*f(n - 1);
        int x2 = f(n - 2);
        return x1 - x2;
    }
}

int main() {
    int x = __VERIFIER_nondet_int();
    int result = f(x);
    if (x >= 1)
        __VERIFIER_assert(result == 3*x - 2);
}
    
