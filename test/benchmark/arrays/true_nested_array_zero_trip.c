extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0 || n > 5) return 0;
    int a[4][5];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 5; ++j) a[i][j] = 42;
    int j = -1;
    for (int i = 0; i < 4; ++i) {
        for (j = 0; j < n; ++j) a[i][j] = i+j;
        __VERIFIER_assert(j == n);
    }
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 5; ++k)
            __VERIFIER_assert(a[i][k] == (k < n ? i+k : 42));
    return 0;
}
