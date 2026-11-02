extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int condition) { if (!condition) __VERIFIER_error(); }

int main(void) {
    for (int i = 0; i < 4; ++i) {
        if (__VERIFIER_nondet_int()) {
            __VERIFIER_assert(0);
            return 1;
        }
    }
    return 0;
}
