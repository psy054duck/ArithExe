extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int x = __VERIFIER_nondet_int();
    if (!(x >= 100 && x <= 1000)) return 0;
    int y = 0;
    while (x != 0) {
        x--;
        y++;
        if (y == 50) break;
    }
    __VERIFIER_assert(y == 50);
    return 0;
}
