extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, y = 0, i = 0;
    while (i < 1) {
        int old = x;
        int j = 0;
        while (j < 2) { ++x; ++j; }
        y = old;
        ++i;
    }
    __VERIFIER_assert(x == y);
}
