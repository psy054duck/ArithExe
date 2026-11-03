extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
extern void abort(void);
void reach_error(void) { abort(); }
int main(void) {
    int a[100000];
    for (int i=0;i<100000;++i) a[i]=__VERIFIER_nondet_int();
    int m=0;
    for (int i=0;i<100000;++i) if (a[i]>m) m=a[i];
    for (int i=0;i<100000;++i) __VERIFIER_assert(m>=a[i]);
    return 0;
}
