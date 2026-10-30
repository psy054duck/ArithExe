extern _Bool __VERIFIER_nondet_bool();
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, y = 0;
    for (int i = 0; i < 3; ++i) {
        _Bool b = __VERIFIER_nondet_bool();
        x += b;
        if (b) ++y;
    }
    __VERIFIER_assert(x + y == 0);
    return 0;
}
