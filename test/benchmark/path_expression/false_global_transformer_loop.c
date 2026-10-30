extern void __VERIFIER_assert(int);
int x;
void step(void) { --x; }
int read_x(void) { return x; }
int main(void) {
    x = 1000000;
    while (x > 0) step();
    __VERIFIER_assert(read_x() == 1);
    return 0;
}
