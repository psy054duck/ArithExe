extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int stop = __VERIFIER_nondet_int();
    if (stop < 3 || stop > 1000000) return 0;
    int x = 0, y = 0;
    while (x < 10000000) {
        ++x;
        y += 2;
        if (x == stop) break;
        if (x == 7) return 0;
    }
    __VERIFIER_assert(x == stop && y == 2*x);
    return 0;
}
