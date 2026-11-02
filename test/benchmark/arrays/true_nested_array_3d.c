extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }

int main(void) {
    int m = 3, n = 4, p = 5;
    int a[m][n][p];
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < p; ++k) a[i][j][k] = 100*i+10*j+k;
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < p; ++k)
                __VERIFIER_assert(a[i][j][k] == 100*i+10*j+k);
    return 0;
}
