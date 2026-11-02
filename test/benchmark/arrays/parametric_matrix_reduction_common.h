extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

static int distance(int x, int y) {
    if (x > y) return x-y;
    return y-x;
}

int main(void) {
    int n=__VERIFIER_nondet_int();
    __VERIFIER_assume(n>1);
    int a[n][n];
    int value=1;
    for (int row=0; row<n; ++row) {
        for (int col=0; col<n; ++col) {
#if MATRIX_KIND == 0
            a[row][col]=row==col ? 0 : col;
#elif MATRIX_KIND == 1
            a[row][col]=distance(row,col);
#else
            if (row%2==col%2) {
                a[row][col]=value;
                value=-value;
            } else a[row][col]=0;
#endif
        }
    }
    long long sum=0;
    for (int row=0; row<n; ++row)
        for (int col=0; col<n; ++col) sum+=a[row][col];
#if MATRIX_KIND == 0
    __VERIFIER_assert(2*sum==(long long)n*(n-1)*(n-1)+WRONG_RESULT);
#elif MATRIX_KIND == 1
    __VERIFIER_assert(3*sum==(long long)n*(n-1)*(n+1)+WRONG_RESULT);
#elif WRONG_RESULT
    __VERIFIER_assert(sum==2);
#else
    __VERIFIER_assert(sum==0 || sum==1);
#endif
    return 0;
}
