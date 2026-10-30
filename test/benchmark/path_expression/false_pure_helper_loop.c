extern void __VERIFIER_assert(int);
unsigned int step(unsigned int x) { return x + 2; }
int main(void) {
    unsigned int x = 0;
    while (x < 1000000) x = step(x);
    __VERIFIER_assert(x % 2 != 0);
    return 0;
}
