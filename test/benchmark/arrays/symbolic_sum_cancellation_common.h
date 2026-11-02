extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int n=__VERIFIER_nondet_int();
    if (n>1) {
        int a[n];
        for (int i=0;i<n;++i) a[i]=__VERIFIER_nondet_int();
        long long sum=0;
        for (int i=0;i<n;++i) sum+=3LL*a[i]+2;
        for (int i=0;i<n;++i) sum-=a[i];
        if (SUM_CHANGED_ARRAY) a[0]+=1;
        for (int i=0;i<n-SUM_CHANGED_BOUND;++i) sum-=2LL*a[i]+2;
        __VERIFIER_assert(sum==SUM_ASSERT_OFFSET);
    }
    return 0;
}
