extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

void increment_array(int *src, int *dst) {
    for (int i = 0; i < 8; ++i) dst[i] = src[i] + 1;
}

int main(void) {
    int src[8], dst[8];
    for (int i = 0; i < 8; ++i) src[i] = __VERIFIER_nondet_int();
    increment_array(src, dst);
    __VERIFIER_assert(dst[5] == src[5]);
    return 0;
}
