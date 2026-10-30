extern void __VERIFIER_assert(int);
int main(void) {
    int a = 0, b = 0;
    while (a < 3) { ++a; ++b; }
    int i = 0, x = 0, u = 0, v = 0;
    while (i < 10) {
        int j = 0;
        while (j < 2) {
            ++x;
            ++u;
            ++v;
            __VERIFIER_assert(x < 2);
            if (j == 1) break;
            ++j;
        }
        if (i == 1) break;
        ++i;
    }
    __VERIFIER_assert(a == b && u == v);
    return 0;
}
