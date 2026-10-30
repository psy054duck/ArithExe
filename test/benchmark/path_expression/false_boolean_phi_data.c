extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int i = 0, sum = 0;
    _Bool a = __VERIFIER_nondet_bool();
    while (i < 2) {
        sum += (int)a;
        ++i;
        a = __VERIFIER_nondet_bool();
    }
    __VERIFIER_assert(sum == 0);
}
