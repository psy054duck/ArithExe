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
    assume_abort_if_not(n <= 20);

    long long x = 1;
    long long y = 2;
    int i = 0;

    /*
     * Generated from h(u,v) = (u, v + u^3) and
     * phi(x,y) = (x + y, x). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + y;
        long long q = u * u * u;
        x = x + q;
        y = y - q;
        i = i + 1;
    }

    __VERIFIER_assert(x + y == 3);
    __VERIFIER_assert(x == 1 + 27 * ((long long)n));
    __VERIFIER_assert(y == 2 - 27 * ((long long)n));
    return 0;
}
