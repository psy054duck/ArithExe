extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 0;
    while (__VERIFIER_nondet_int()) {
        x += 4;
        __VERIFIER_assert(x % 3 == 0);
    }
    return 0;
}
