extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0 || n > 1000000) return 0;
    int i = 0, t = 0;
    while (i < n) {
        t = i + 1;
        ++i;
        if (i == 6) break;
        if (i == 7) return 0;
    }
    __VERIFIER_assert(t == i);
    return 0;
}
