extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0;
    while (__VERIFIER_nondet_bool()) {
        if (x == 0) x++;
        else if (x == 1) x++;
        else x -= 2;
    }
    __VERIFIER_assert(x >= 0 && x <= 2);
    return 0;
}
