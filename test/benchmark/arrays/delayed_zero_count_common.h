extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);
int N = 1000;

int main(void) {
    int a[N], k = 0, j = 0;
    int lim = __VERIFIER_nondet_int();
    __VERIFIER_assume(200 < lim && lim < N/3);
    for (int i = 0; i < N; ++i) {
        a[i] = i+1;
        if (i > N/2 && k < lim) a[i] = i%3;
        if (a[i] == 0) ++k;
    }
    for (int i = 0; i < N; ++i) {
        if (a[i] == 0) ++j;
        __VERIFIER_assert(DELAYED_COUNT_BOUND);
    }
    return 0;
}
