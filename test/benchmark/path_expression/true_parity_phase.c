extern void abort(void);
extern int __VERIFIER_nondet_int(void);

void reach_error(void) { abort(); }

void __VERIFIER_assert(int condition) {
    if (!condition) reach_error();
}

void __VERIFIER_assume(int condition) {
    if (!condition) abort();
}

int main(void) {
    int x = __VERIFIER_nondet_int();
    int iterations = __VERIFIER_nondet_int();
    __VERIFIER_assume(0 <= x && x <= 20);
    __VERIFIER_assume(2 <= iterations && iterations <= 12);

    const int initial_x = x;
    for (int i = 0; i < iterations; ++i) {
        if (x % 2 == 0) {
            x = x + 1;
        } else {
            x = x + 2;
        }
    }

    __VERIFIER_assert(initial_x + iterations <= x);
    __VERIFIER_assert(x <= initial_x + 2 * iterations);
    return 0;
}
