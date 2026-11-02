extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
extern void abort(void) __attribute__((noreturn));
void __VERIFIER_assert(int condition) { if (!condition) __VERIFIER_error(); }
void assume_abort_if_not(int condition) { if (!condition) abort(); }

int main(void) {
    int i;
    for (i = 0; i < 8; ++i) {
        if (__VERIFIER_nondet_int()) break;
    }
    assume_abort_if_not(i >= 3);
    int count = 0;
    for (int k = 1; k < i; ++k) ++count;
    __VERIFIER_assert(count == i);
    return 0;
}
