extern void __VERIFIER_assert(int);
int main(void) {
    int p = 1, q = 2, i = 0;
    while (i < 1) {
        while (p < 2) { int old = p; p = q; q = old; }
        ++i;
    }
    __VERIFIER_assert(p == q);
}
