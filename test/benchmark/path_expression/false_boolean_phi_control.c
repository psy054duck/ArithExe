extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 1, y = 1;
    _Bool a = __VERIFIER_nondet_bool();
    while (x > 0 && y > 0) {
        if (a) --x; else --y;
        a = __VERIFIER_nondet_bool();
    }
    __VERIFIER_assert(x == 0);
}
