extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void abort(void) __attribute__((noreturn));

void assume_abort_if_not(int condition) {
    if (!condition) abort();
}

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int main(void) {
    for (int i = 0; i < 2; ++i) {
        int k = __VERIFIER_nondet_int();
        __VERIFIER_assert(i != 0);
        assume_abort_if_not(i == 0 && k > 0);
    }
    return 0;
}
