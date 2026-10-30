extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0;
    while (!__VERIFIER_nondet_int()) ++x;
    __VERIFIER_assert(x <= 1);
    return 0;
}
