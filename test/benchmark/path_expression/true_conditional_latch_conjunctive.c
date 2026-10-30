extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    if (!(x == y && y >= 0)) return 0;
    while (x != 0) {
        x--;
        y--;
        if (x < 0 || y < 0) break;
    }
    __VERIFIER_assert(y == 0);
    return 0;
}
