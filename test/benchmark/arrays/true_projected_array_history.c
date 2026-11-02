extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[2];
    for (int i=0; i<2; ++i) a[i]=__VERIFIER_nondet_int();
    int entry=a[0];
    for (int i=0; i<2; ++i) {
        if (i==0) __VERIFIER_assume(a[0]==0);
        a[0]=1;
    }
    __VERIFIER_assert(entry==0);
    return 0;
}
