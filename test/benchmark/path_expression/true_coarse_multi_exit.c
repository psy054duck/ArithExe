extern int __VERIFIER_nondet_int(void);
extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0 || n > 10000000) return 0;
    int x = 0, y = 0;
    while (x < n) {
        ++x;
        y += 2;
        if (x == 17) break;
        if (__VERIFIER_nondet_bool()) return 0;
    }
    __VERIFIER_assert(y == 2*x);
    return 0;
}
