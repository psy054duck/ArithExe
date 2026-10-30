extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int a = 0, b = 0;
    for (int i = 0; i < 4; ++i) {
        if (__VERIFIER_nondet_int()) { a++; b += 2; }
        else { a += 2; b++; }
    }
    __VERIFIER_assert(a == b);
    return 0;
}
