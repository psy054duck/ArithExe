extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, t = 0;
    while (__VERIFIER_nondet_bool()) {
        if (x == 0) x++;
        else x--;
        t++;
    }
    if (t >= 8) __VERIFIER_assert(x == 0);
    return 0;
}
