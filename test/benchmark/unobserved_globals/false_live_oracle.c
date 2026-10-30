extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int dead_spill;
int main(void) {
    int x = 0;
    for (int i = 0; i < 3; ++i) {
        int choice = __VERIFIER_nondet_int();
        dead_spill = choice;
        if (choice) x += choice;
    }
    __VERIFIER_assert(x == 0);
    return 0;
}
