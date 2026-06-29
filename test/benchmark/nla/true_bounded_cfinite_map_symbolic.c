extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *)
    __attribute__((__nothrow__, __leaf__)) __attribute__((__noreturn__));

void reach_error() { __assert_fail("0", "true_bounded_cfinite_map_symbolic.c", 4, "reach_error"); }
extern int __VERIFIER_nondet_int(void);

void assume_abort_if_not(int cond) {
    if (!cond) {
        abort();
    }
}

void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR:
        { reach_error(); }
    }
}

int main() {
    int n = __VERIFIER_nondet_int();
    assume_abort_if_not(n >= 0);
    assume_abort_if_not(n <= 20);

    long long x = 2;
    long long y = 3;
    int i = 0;

    while (i < n) {
        long long s = x + y;
        long long q = s * s;
        x = x + q;
        y = y - q;
        i = i + 1;
    }

    __VERIFIER_assert(x == 2 + ((long long)n) * 25);
    __VERIFIER_assert(y == 3 - ((long long)n) * 25);
    return 0;
}
