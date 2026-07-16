extern void abort(void);

void reach_error() { abort(); }
extern int __VERIFIER_nondet_int(void);

void assume_abort_if_not(int cond) {
    if (!cond) {
        abort();
    }
}

void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR:
        { reach_error(); abort(); }
    }
}

int main() {
    int n = __VERIFIER_nondet_int();
    assume_abort_if_not(n >= 0);
    assume_abort_if_not(n <= 1000);

    long long x = 3;
    long long y = 1;
    int i = 0;

    /*
     * Generated from h(u,v) = (u, v + u^2) and
     * phi(x,y) = (x + 2y, y). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + 2 * y;
        long long q = u * u;
        y = y + q;
        x = x - 2 * q;
        i = i + 1;
    }

    __VERIFIER_assert(x + 2 * y == 5);
    __VERIFIER_assert(x == 3 - 50 * ((long long)n));
    __VERIFIER_assert(y == 1 + 25 * ((long long)n));
    return 0;
}
