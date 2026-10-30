extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 0;
    do {
        x = __VERIFIER_nondet_int();
        __VERIFIER_assert(x % 3 == 0);
    } while (__VERIFIER_nondet_int());
    return 0;
}
