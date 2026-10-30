extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int n = __VERIFIER_nondet_uint(), x = n, y = 0, z;
    while (x > 0) { x--; y++; }
    z = y;
    while (z > 0) { x++; z--; }
    while (y > 0) { y--; z++; }
    while (x > 0) { x--; y++; }
    while (z > 0) { x++; z--; }
    __VERIFIER_assert(x == n);
    return 0;
}
