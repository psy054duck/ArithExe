extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

// Integer-relaxed regression for the processed nla-digbench Bresenham task.
int main(void) {
    int X = __VERIFIER_nondet_int(), Y = __VERIFIER_nondet_int();
    long long x = 0, y = 0, v = 2LL * Y - X;
    while (x <= X) {
        if (v < 0) v += 2LL * Y;
        else { v += 2LL * (Y - X); y++; }
        x++;
    }
    __VERIFIER_assert(2LL * Y * x - 2LL * x * y - X + 2LL * Y - v + 2LL * y == 0);
    return 0;
}
