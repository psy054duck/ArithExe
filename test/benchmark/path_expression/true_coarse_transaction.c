extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, y = 0, i = 0;
    while (i < 2) {
        if (__VERIFIER_nondet_bool()) x++;
        else y++;
        i++;
    }
    __VERIFIER_assert(x + y == i);
    // An imprecise coarse successor can take the first branch. Both coarse
    // siblings and the earlier successful assertion must be rolled back.
    if (x < 0) __VERIFIER_assert(0);
    else __VERIFIER_assert(x <= 2);
    return 0;
}
