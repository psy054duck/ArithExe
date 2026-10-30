extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, i = 0, j = 0;
    while (i < 1) {
        while (j < 2) { x += __VERIFIER_nondet_bool(); ++j; }
        ++i;
    }
    __VERIFIER_assert(x >= 0 && x <= 2);
}
