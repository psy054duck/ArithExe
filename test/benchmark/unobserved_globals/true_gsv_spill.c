extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int spill;
int main(void) {
    int x = -50, y = __VERIFIER_nondet_int();
    if (y <= -1000 || y >= 1000000) return 0;
    while (x < 0) {
        x += y;
        spill = y;
        y++;
    }
    __VERIFIER_assert(y > 0);
    return 0;
}
