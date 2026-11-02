extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }

int main(void) {
    int a[6][8];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 8; ++j) {
            a[i][j] = i+j;
            __VERIFIER_assert(i < 4 || a[i][j] != i+j);
        }
    return 0;
}
