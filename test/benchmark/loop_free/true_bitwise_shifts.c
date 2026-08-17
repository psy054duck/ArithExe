extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);

static void check(int condition) {
    if (!condition) reach_error();
}

int main(void) {
    volatile int raw = __VERIFIER_nondet_int();

    if (raw == 6) {
        check((raw & 3) == 2);
        check((raw | 1) == 7);
        check((raw ^ 3) == 5);
        check((raw << 3) == 48);
    }

    if (raw == -1) {
        unsigned int value = (unsigned int)raw;
        check((value >> 1) == 2147483647u);
        check((value ^ 255u) == 4294967040u);
    }

    if (raw == -8) check((raw >> 2) == -2);
    return 0;
}
