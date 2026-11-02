extern void __VERIFIER_assert(int);

void increment_array(int *src, int *dst, int n) {
    for (int i = 0; i < n; ++i) dst[i] = src[i] + 1;
}

int main(void) {
    int src[10], dst[10];
    for (int i = 0; i < 10; ++i) {
        src[i] = 2 * i;
        dst[i] = -17;
    }
    increment_array(src + 1, dst + 2, 6);
    __VERIFIER_assert(dst[2] == 4);
    return 0;
}
