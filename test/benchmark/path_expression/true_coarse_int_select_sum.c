extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 0, y = 0;
    while (__VERIFIER_nondet_int()) {
        unsigned int step = __VERIFIER_nondet_int() ? 4 : 5;
        x += step;
        y += step;
    }
    __VERIFIER_assert(x == y);
    return 0;
}
