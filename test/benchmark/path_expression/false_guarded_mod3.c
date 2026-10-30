extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 1, y = 1;
    while (__VERIFIER_nondet_int()) {
        if (x % 3 == 1) { x += 1; y = 0; } /* deliberately wrong */
        else if (x % 3 == 2) { x += 1; y = 0; }
        else { x += __VERIFIER_nondet_int() ? 4 : 5; y = 1; }
    }
    if (y == 0) __VERIFIER_assert(x % 3 == 0);
    return 0;
}
