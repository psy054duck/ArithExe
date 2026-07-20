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
    assume_abort_if_not(n <= 5);

    long long x = 1;
    long long y = 2;
    long long z = 3;
    int i = 0;

    /*
     * Generated from h(u,v,w) = (u, v + u^2, w + u^3) and
     * phi(x,y,z) = (x + y + z, x, y). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + y + z;
        long long q2 = u * u;
        long long q3 = q2 * u;
        x = x + q2;
        y = y + q3;
        z = z - q2 - q3;
        i = i + 1;
    }

    __VERIFIER_assert(x + y + z == 6);
    __VERIFIER_assert(x == 1 + 36 * ((long long)n));
    __VERIFIER_assert(y == 2 + 216 * ((long long)n));
    __VERIFIER_assert(z == 3 - 252 * ((long long)n));
    return 0;
}
