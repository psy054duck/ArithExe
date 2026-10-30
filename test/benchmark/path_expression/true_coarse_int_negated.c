extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int x = 0, y = 0, a = 0, b = 0;
    while (!__VERIFIER_nondet_int()) {
        if (0 == __VERIFIER_nondet_int()) { ++x; ++y; }
        else { ++a; ++b; }
    }
    __VERIFIER_assert(x == y && a == b);
    return 0;
}
