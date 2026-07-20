extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *)
    __attribute__((__nothrow__, __leaf__)) __attribute__((__noreturn__));

void reach_error() { __assert_fail("0", "true_bounded_cfinite_map_fixed.c", 4, "reach_error"); }

void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR:
        { reach_error(); }
    }
}

int main() {
    int x = 2;
    int y = 3;
    int i = 0;

    while (i < 20) {
        int s = x + y;
        int q = s * s;
        x = x + q;
        y = y - q;
        i = i + 1;
    }

    __VERIFIER_assert(x == 2 + 20 * 25);
    __VERIFIER_assert(y == 3 - 20 * 25);
    return 0;
}
