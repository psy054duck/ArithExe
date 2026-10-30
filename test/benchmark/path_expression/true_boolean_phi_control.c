extern unsigned int __VERIFIER_nondet_uint(void);
extern _Bool __VERIFIER_nondet_bool(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = __VERIFIER_nondet_uint();
    unsigned int y = __VERIFIER_nondet_uint();
    unsigned int z = __VERIFIER_nondet_uint();
    _Bool a = __VERIFIER_nondet_bool(), b = __VERIFIER_nondet_bool();
    while (x > 0 && y > 0 && z > 0) {
        if (a) --x;
        else if (b) --y;
        else --z;
        a = __VERIFIER_nondet_bool();
        b = __VERIFIER_nondet_bool();
    }
    __VERIFIER_assert(x == 0 || y == 0 || z == 0);
}
