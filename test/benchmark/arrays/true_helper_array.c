extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

void increment_array(int *src, int *dst) {
    for (int i = 0; i < 8; ++i) dst[i] = src[i] + 1;
}

int main(void) {
    int src[8], dst[8];
    for (int i = 0; i < 8; ++i) src[i] = __VERIFIER_nondet_int();
    increment_array(src, dst);
    for (int i = 0; i < 8; ++i)
        __VERIFIER_assert(dst[i] == src[i] + 1);
    return 0;
}
