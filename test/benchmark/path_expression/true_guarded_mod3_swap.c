extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 1, y = 2;
    while (__VERIFIER_nondet_int()) {
        unsigned int old_x = x;
        x = y;
        y = old_x;
    }
    __VERIFIER_assert(x % 3 + y % 3 == 3);
    return 0;
}
