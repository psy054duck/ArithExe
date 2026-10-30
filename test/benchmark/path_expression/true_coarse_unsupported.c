extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int x = 1, i = 0;
    while (i < 3) {
        // Nondeterministic *data* updates are outside the polynomial-map
        // profile. A failed optimization must retain ordinary execution.
        x += __VERIFIER_nondet_bool();
        i++;
    }
    __VERIFIER_assert(x >= 1 && x <= 4);
    return 0;
}
