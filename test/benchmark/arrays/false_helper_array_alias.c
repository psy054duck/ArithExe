extern void __VERIFIER_assert(int);

void increment_array(int *src, int *dst) {
    for (int i = 0; i < 8; ++i) dst[i] = src[i] + 1;
}

int main(void) {
    int a[8];
    for (int i = 0; i < 8; ++i) a[i] = 2 * i;
    increment_array(a, a);
    __VERIFIER_assert(a[5] == 10);
    return 0;
}
