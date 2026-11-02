extern void __VERIFIER_error(void);

int main(void) {
    int a[2];
    for (int i = 0; i < 2; ++i) a[i] = 0;
    for (int i = 0; i < 2; ++i) {
        if (a[i] == 0) __VERIFIER_error();
    }
    return 0;
}
