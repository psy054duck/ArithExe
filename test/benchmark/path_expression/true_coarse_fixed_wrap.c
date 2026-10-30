extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);

int main(void) {
    unsigned int x = 4294967294U, y = 2U;
    int i = 0;
    while (i < 2) {
        if (__VERIFIER_nondet_bool()) x++;
        else y++;
        i++;
    }
    __VERIFIER_assert(x + y == (unsigned int)i);
    return 0;
}
