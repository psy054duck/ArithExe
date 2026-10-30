extern void __VERIFIER_assert(int);
int main(void) {
    int i = 0, x = 0, y = 0, a = 0, b = 0;
    while (i < 10) {
        int j = 0;
        while (j < 10) {
            ++x;
            ++a;
            ++b;
            if (j == 1) break;
            ++y;
            ++j;
        }
        if (i == 1) break;
        ++i;
    }
    __VERIFIER_assert(x == y && a == b);
    return 0;
}
