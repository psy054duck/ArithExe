extern int __VERIFIER_nondet_int();
extern _Bool __VERIFIER_nondet_bool();
extern void __VERIFIER_assert(int);
int main(void) {
    int x = __VERIFIER_nondet_int(), z = __VERIFIER_nondet_int();
    if (x <= -100 || x >= 200 || z <= 100 || z >= 200) return 0;
    while (x < 100 && z > 100) {
        if (__VERIFIER_nondet_bool()) ++x;
        else { --x; --z; }
    }
    __VERIFIER_assert(x >= 100 || z <= 100);
    return 0;
}
