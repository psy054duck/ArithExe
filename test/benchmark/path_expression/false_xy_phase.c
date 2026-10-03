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
    int y = __VERIFIER_nondet_int();
    int iterations = __VERIFIER_nondet_int();
    __VERIFIER_assume(0 <= x && x <= 8);
    __VERIFIER_assume(0 <= y && y <= 8);
    __VERIFIER_assume(2 <= iterations && iterations <= 12);

    const int initial_sum = x + y;
    for (int i = 0; i < iterations; ++i) {
        if (x < y) {
            x = x + 1;
        } else {
            y = y + 1;
        }
    }

    __VERIFIER_assert(x + y == initial_sum + iterations + 1);
    return 0;
}
