extern void __VERIFIER_assert(int);
int main(void) {
    int i = 0, j = 0;
    while (i < 1) {
        while (j < 2) { ++j; __VERIFIER_assert(j < 2); }
        ++i;
    }
    __VERIFIER_assert(i == 1);
}
