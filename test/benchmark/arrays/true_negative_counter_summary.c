extern void __VERIFIER_error(void) __attribute__((noreturn));

int main(void) {
    int a[4];
    for (int k = 0; k < 4; ++k) a[k] = 1;
    a[0] = 5;
    int sum = 0;
    for (int i = -1; i < 3; ++i) sum += a[i + 1];
    if (sum != 8) __VERIFIER_error();
    return 0;
}
