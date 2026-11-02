extern void abort(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }
void assume(int c) { if (!c) abort(); }

int main(void) {
    int a[3][4];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) {
            a[i][j] = i+j;
            __VERIFIER_assert(i != 0 || j != 0);
            assume(j == 0);
        }
    return 0;
}
