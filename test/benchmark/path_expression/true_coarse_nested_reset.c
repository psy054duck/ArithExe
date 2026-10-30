extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
extern void __VERIFIER_assume(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    __VERIFIER_assume(n >= 0 && n <= 1000);
    int x = 0, y = 0;
    while (x < n) {
        int j = 0;
        while (j < 3) { ++x; ++y; ++j; }
    }
    __VERIFIER_assert(x == y);
}
