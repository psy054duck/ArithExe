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
    long long y = 1;
    int i = 0;

    /*
     * Generated from h(u,v) = (u + 1, v + u) and
     * phi(x,y) = (x + y^2, y). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + y * y;
        long long next_y = y + u;
        long long next_x = u + 1 - next_y * next_y;
        x = next_x;
        y = next_y;
        i = i + 1;
    }

    __VERIFIER_assert(x + y * y == 2 + (long long)n);
    __VERIFIER_assert(2 * y == 2 + ((long long)n) * ((long long)n + 3));
    return 0;
}
