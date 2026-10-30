extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, y = 0;
    while (__VERIFIER_nondet_bool()) {
        ++x;
        ++y;
        if (x == 8) break;
        if (__VERIFIER_nondet_bool()) return 0;
    }
    __VERIFIER_assert(x == y);
    return 0;
}
