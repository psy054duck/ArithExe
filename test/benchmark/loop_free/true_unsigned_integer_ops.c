extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);

static void check(int condition) {
    if (!condition) reach_error();
}

int main(void) {
    volatile int raw = __VERIFIER_nondet_int();
    volatile unsigned int value = (unsigned int)raw;

    if (raw < 0) check(value > 0);

    if (raw == -2) {
        check(value / 2u == 2147483647u);
        check(value % 3u == 2u);
    }

    if (raw == 2147483647) {
        check(value + 1u == 2147483648u);
    }
    return 0;
}
