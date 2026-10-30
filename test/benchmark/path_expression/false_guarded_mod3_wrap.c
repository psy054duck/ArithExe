extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 4294967295u, count = 0;
    while (__VERIFIER_nondet_bool() && count < 1) { x++; count++; }
    /* A mathematical-integer-only transfer would incorrectly prove this. */
    __VERIFIER_assert(count == 0 || x % 3 == 1);
    return 0;
}
