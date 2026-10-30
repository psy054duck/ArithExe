extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int spill;
int main(void) {
    int i = 0, x = 0, y = 0;
    while (i < 20) {
        int choice = __VERIFIER_nondet_int();
        int step = choice ? 4 : 5;
        x += step;
        y += step;
        spill = choice;
        i++;
    }
    __VERIFIER_assert(x == y);
    return 0;
}
