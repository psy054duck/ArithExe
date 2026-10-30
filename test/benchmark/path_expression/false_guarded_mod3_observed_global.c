extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
unsigned int observed;
int main(void) {
    unsigned int count = 0;
    while (__VERIFIER_nondet_bool() && count < 1) {
        observed = 1;
        count++;
    }
    __VERIFIER_assert(observed % 3 == 0);
    return 0;
}
