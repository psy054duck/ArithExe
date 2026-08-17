extern int __VERIFIER_nondet_int(void);
extern unsigned int __VERIFIER_nondet_uint(void);
extern void reach_error(void);

static void check(int condition) {
    if (!condition) reach_error();
}

int main(void) {
    int shift = __VERIFIER_nondet_int();
    unsigned int value = __VERIFIER_nondet_uint();

    if (shift >= 32) {
        unsigned int shifted = value << shift;
        check(shifted == 1234567u);
    }
    return 0;
}
