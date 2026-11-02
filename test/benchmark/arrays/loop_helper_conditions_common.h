extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

static void check_parity(int value) {
    if (value % 2 == 0) {
        __VERIFIER_assert(value % 2 == EXPECTED_EVEN_REMAINDER);
    } else {
        __VERIFIER_assert(value % 2 != 0);
    }
}

int main(void) {
    int a[8];
    for (int i = 0; i < 8; ++i) a[i] = __VERIFIER_nondet_int();
    for (int i = 0; i < 8; ++i) check_parity(a[i]);
    return 0;
}
