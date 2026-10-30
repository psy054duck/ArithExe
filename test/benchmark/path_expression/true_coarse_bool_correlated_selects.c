extern _Bool __VERIFIER_nondet_bool();
extern void __VERIFIER_assert(int);
int main(void) {
    int a = 0, b = 0;
    for (int i = 0; i < 1000000; ++i) {
        if (__VERIFIER_nondet_bool()) { ++a; b += 2; }
        else { a += 2; ++b; }
    }
    __VERIFIER_assert(a + b == 3000000);
    return 0;
}
