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

    long long x = 2;
    long long y = 3;
    int i = 0;

    /*
     * Generated from h(u,v) = (u, v + u^2) and
     * phi(x,y) = (x + y, x). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + y;
        long long q = u * u;
        x = x + q;
        y = y - q;
        i = i + 1;
    }

    __VERIFIER_assert(x + y == 5);
    __VERIFIER_assert(x == 2 + ((long long)n) * 25);
    __VERIFIER_assert(y == 3 - ((long long)n) * 25);
    return 0;
}
