extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int,
                          const char *) __attribute__((noreturn));

void reach_error(void) {
    __assert_fail("0", "true_ghost_initial.c", 6, "reach_error");
}

void __VERIFIER_assert(int condition) {
    if (!condition) {
        reach_error();
        abort();
    }
}

extern int __VERIFIER_nondet_int(void);

int main(void) {
    int value = __VERIFIER_nondet_int();
    int expected = value + 6;
    int iteration = 0;
    while (iteration < 3) {
        value += 2;
        ++iteration;
    }
    __VERIFIER_assert(value == expected);
    return 0;
}
