extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[3];
    for (int i = 0; i < 3; ++i) a[i] = __VERIFIER_nondet_int();
    __VERIFIER_assert(a[0] != a[0]);
    return 0;
}
