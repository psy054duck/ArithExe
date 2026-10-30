extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    if (__VERIFIER_nondet_bool()) {
        __VERIFIER_assert(0);
    } else {
        long long x = 0, y = 0;
        while (x < 10) {
            y += 3*x*x + 3*x + 1;
            x++;
        }
        __VERIFIER_assert(y == x*x*x);
    }
    return 0;
}
