extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int a[5];
    for (int i = 0; i < 5; ++i) a[i] = __VERIFIER_nondet_int();
    int sum = 7;
    for (int i = 0; i < 5; ++i) sum += a[i];
    __VERIFIER_assert(sum == 8 + a[0]+a[1]+a[2]+a[3]+a[4]);
    return 0;
}
