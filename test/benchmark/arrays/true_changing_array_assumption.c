extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[1];
    a[0]=__VERIFIER_nondet_int();
    for (int i=0; i<2; ++i) {
        if (i==0) __VERIFIER_assume(a[0]==0);
        else __VERIFIER_assert(a[0]==1);
        a[0]=1;
    }
    return 0;
}
