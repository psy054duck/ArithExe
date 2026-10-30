extern void __VERIFIER_assert(int);
int observed;
__attribute__((noinline)) int read_observed(void) { return observed; }
int main(void) {
    for (int i = 0; i < 4; ++i) observed = i;
    __VERIFIER_assert(read_observed() == 0);
    return 0;
}
