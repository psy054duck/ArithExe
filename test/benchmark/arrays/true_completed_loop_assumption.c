extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[2];
    for (int i=0; i<2; ++i) a[i]=__VERIFIER_nondet_int();
    for (int i=0; i<2; ++i) __VERIFIER_assume(a[i]==7);
    __VERIFIER_assert(a[0]==7 && a[1]==7);
    return 0;
}
