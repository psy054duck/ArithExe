extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }

int main(void) {
    int m = 4, n = 5;
    int a[m][n];
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) a[i][j] = -9;
    for (int i = 0; i < m; ++i)
        for (int j = 1; j < n-1; ++j) a[i][j] = 10*i+j;
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            __VERIFIER_assert(a[i][j] == ((j == 0 || j == n-1) ? -9 : 10*i+j));
    return 0;
}
