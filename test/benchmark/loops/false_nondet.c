void reach_error(void) {}

extern int __VERIFIER_nondet_int(void);

int main(void) {
    int iterations = __VERIFIER_nondet_int();
    if (iterations < 1 || iterations > 3) {
        return 0;
    }

    int value = 0;
    for (int i = 0; i < iterations; ++i) {
        value = __VERIFIER_nondet_int();
    }

    (void)value;
    reach_error();
    return 0;
}
