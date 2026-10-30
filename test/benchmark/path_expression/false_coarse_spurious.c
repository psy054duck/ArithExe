extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int x = 0, y = 0, i = 0;
    while (i < 100000000) {
        if (__VERIFIER_nondet_bool()) x++;
        else y++;
        i++;
    }
    // The coarse sum relation cannot justify a concrete branch history.
    __VERIFIER_assert(x == y);
    return 0;
}
