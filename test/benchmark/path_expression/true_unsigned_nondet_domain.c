extern unsigned int __VERIFIER_nondet_uint(void);
extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int n = __VERIFIER_nondet_uint();
    if (n > 100) return 0;
    int signed_n = __VERIFIER_nondet_int();
    if (signed_n != -1) return 0;
    unsigned int x = 0;
    while (x < n) ++x;
    __VERIFIER_assert(x == n);
    __VERIFIER_assert(signed_n == -1);
    return 0;
}
