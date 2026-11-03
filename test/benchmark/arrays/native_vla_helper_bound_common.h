extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);
long long width;

static int distance(int x,int y) {
#if MUTATE_BOUND
    width=1;
#endif
    return x>y ? x-y : y-x;
}

int main(void) {
    width=__VERIFIER_nondet_int();
    __VERIFIER_assume(width>1);
    int a[width][width];
    for (int i=0;i<width;++i)
        for (int j=0;j<width;++j) a[i][j]=distance(i,j);
#if MUTATE_BOUND
    __VERIFIER_assert(width>1);
#else
    __VERIFIER_assert(a[0][1]==1);
#endif
    return 0;
}
