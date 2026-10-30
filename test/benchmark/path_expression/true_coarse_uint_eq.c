extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);

int main(void) {
    unsigned int w = __VERIFIER_nondet_uint(), x = w;
    unsigned int y = __VERIFIER_nondet_uint(), z = y;
    while (__VERIFIER_nondet_uint()) {
        if (__VERIFIER_nondet_uint()) { ++w; ++x; }
        else { --y; --z; }
    }
    __VERIFIER_assert(w == x && y == z);
    return 0;
}
