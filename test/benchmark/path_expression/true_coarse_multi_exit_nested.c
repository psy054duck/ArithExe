extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0 || n > 1000000) return 0;
    int i = 0, x = 0, y = 0;
    while (i < n) {
        int j = 0;
        while (j < 10) {
            ++x;
            ++y;
            __VERIFIER_assert(x == y);
            if (j == 4) break;
            ++j;
        }
        if (i == 7) break;
        ++i;
    }
    __VERIFIER_assert(x == y);
    return 0;
}
