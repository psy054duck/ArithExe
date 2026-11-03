extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

int main(void) {
    int n=__VERIFIER_nondet_int(),m=__VERIFIER_nondet_int();
    __VERIFIER_assume(n>1&&m>1);
#if VLA_RANK == 2
    int a[n][m];
    for (int i=0;i<n;++i)
        for (int j=0;j<m;++j) a[i][j]=3*i+j;
    for (int i=0;i<n;++i)
        for (int j=0;j<m;++j)
            __VERIFIER_assert(a[i][j]==3*i+j+WRONG_RESULT);
#else
    int p=__VERIFIER_nondet_int();
    __VERIFIER_assume(p>1);
    int a[n][m][p];
    for (int i=0;i<n;++i)
        for (int j=0;j<m;++j)
            for (int k=0;k<p;++k) a[i][j][k]=3*i+2*j+k;
    for (int i=0;i<n;++i)
        for (int j=0;j<m;++j)
            for (int k=0;k<p;++k)
                __VERIFIER_assert(a[i][j][k]==3*i+2*j+k+WRONG_RESULT);
#endif
    return 0;
}
