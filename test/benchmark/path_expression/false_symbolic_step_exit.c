extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void __VERIFIER_assert(int condition) { if (!condition) abort(); }
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    if (y <= 0 || y > 1000000 || x >= 100) return 0;
    while (x < 100) x += y;
    __VERIFIER_assert(x == 100);
    return 0;
}
