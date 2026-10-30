extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);

int main(void) {
    int x = 0, y = 0, i = 0;
    while (i < 3) {
        if (__VERIFIER_nondet_bool()) x++;
        else y++;
        i++;
    }
    __VERIFIER_assert(x + y == i);
    return 0;
}
