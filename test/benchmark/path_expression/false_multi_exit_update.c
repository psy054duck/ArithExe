extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, y = 0;
    while (x < 10000000) {
        ++x;
        y += 2;
        if (x == 6) break;
        if (x == 7) return 0;
    }
    __VERIFIER_assert(y == 2*(x-1));
    return 0;
}
