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
    assume_abort_if_not(n <= 4);

    long long x = 1;
    long long y = 1;
    long long z = 1;
    int i = 0;

    /*
     * Generated from h(u,v,w) = (u + 1, v + u, w + v) and
     * phi(x,y,z) = (x + y^2, y + z^2, z). All eigenvalues are 1.
     */
    while (i < n) {
        long long u = x + y * y;
        long long v = y + z * z;
        long long w = z;
        long long next_u = u + 1;
        long long next_v = v + u;
        long long next_w = w + v;
        long long next_z = next_w;
        long long next_y = next_v - next_z * next_z;
        long long next_x = next_u - next_y * next_y;
        x = next_x;
        y = next_y;
        z = next_z;
        i = i + 1;
    }

    __VERIFIER_assert(x + y * y == 2 + (long long)n);
    __VERIFIER_assert(2 * (y + z * z) == 4 + ((long long)n) * ((long long)n + 3));
    __VERIFIER_assert(
        6 * z == 6 + 12 * ((long long)n)
               + 6 * ((long long)n) * ((long long)n - 1)
               + ((long long)n) * ((long long)n - 1) * ((long long)n - 2));
    return 0;
}
