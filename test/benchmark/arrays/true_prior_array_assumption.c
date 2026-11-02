extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[2];
    for (int i=0; i<2; ++i) a[i]=__VERIFIER_nondet_int();
    for (int i=0; i<2; ++i) {
        if (i==0) __VERIFIER_assume(a[0]==a[1]);
        else __VERIFIER_assert(a[0]==a[1]);
    }
    return 0;
}
