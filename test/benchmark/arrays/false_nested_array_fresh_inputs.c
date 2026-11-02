extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));
void __VERIFIER_assert(int c) { if (!c) __VERIFIER_error(); }

int main(void) {
    int a[3][4];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) a[i][j] = __VERIFIER_nondet_int();
    __VERIFIER_assert(a[0][0] == a[1][0]);
    return 0;
}
