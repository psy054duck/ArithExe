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
    assume_abort_if_not(n <= 3);

    long long x = 2;
    long long y = 1;
    long long z = 1;
    int i = 0;

    /*
     * Generated from h(u,v,w) = (u, v + u, w + v^2) and
     * phi(x,y,z) = (x + y^2, y + z, z). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + y * y;
        long long v = y + z;
        long long next_v = v + u;
        long long next_z = z + v * v;
        long long next_y = next_v - next_z;
        long long next_x = u - next_y * next_y;
        x = next_x;
        y = next_y;
        z = next_z;
        i = i + 1;
    }

    __VERIFIER_assert(x + y * y == 3);
    __VERIFIER_assert(y + z == 2 + 3 * ((long long)n));
    __VERIFIER_assert(
        6 * z == 6 + 24 * ((long long)n)
               + 36 * ((long long)n) * ((long long)n - 1)
               + 9 * ((long long)n) * ((long long)n - 1) * (2 * ((long long)n) - 1));
    return 0;
}
