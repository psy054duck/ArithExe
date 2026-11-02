extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[100];
    for (int i=0; i<100; ++i) a[i]=__VERIFIER_nondet_int();
    for (int i=0; i<50; ++i) {
        if (a[2*i]>2*i) a[2*i]=2*i;
    }
    for (int i=0; i<100; ++i) __VERIFIER_assert(a[i]<=i);
    return 0;
}
