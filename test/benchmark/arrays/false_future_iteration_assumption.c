extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

int main(void) {
    int x=__VERIFIER_nondet_int(), y=__VERIFIER_nondet_int();
    for (int i=0; i<2; ++i) {
        if (i==0) __VERIFIER_assert(x==y);
        else __VERIFIER_assume(x==y);
    }
    return 0;
}
