extern _Bool __VERIFIER_nondet_bool();
extern void __VERIFIER_assert(int);
int main(void) {
    int x = 99, z = 101;
    while (x < 100 && z > 100) {
        if (__VERIFIER_nondet_bool()) ++x;
        else { --x; --z; }
    }
    __VERIFIER_assert(x >= 100);
    return 0;
}
