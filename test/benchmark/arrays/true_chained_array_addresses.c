extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }

int main(void) {
    int m = 2, n = 3;
    volatile int a[m][n];
    a[0][0] = 11;
    a[1][0] = 22;
    a[1][2] = 33;
    __VERIFIER_assert(a[0][0] == 11);
    __VERIFIER_assert(a[1][0] == 22);
    __VERIFIER_assert(a[1][2] == 33);
    volatile int b[2][3][4];
    b[0][0][0] = 44;
    b[1][0][0] = 55;
    b[1][2][3] = 66;
    __VERIFIER_assert(b[0][0][0] == 44);
    __VERIFIER_assert(b[1][0][0] == 55);
    __VERIFIER_assert(b[1][2][3] == 66);
    return 0;
}
