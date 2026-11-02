extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[5];
    for (int i = 0; i < 5; ++i) a[i] = __VERIFIER_nondet_int();
    int product = 2;
    for (int i = 0; i < 5; ++i) product *= a[i];
    __VERIFIER_assert(product == 1 + 2*a[0]*a[1]*a[2]*a[3]*a[4]);
    return 0;
}
