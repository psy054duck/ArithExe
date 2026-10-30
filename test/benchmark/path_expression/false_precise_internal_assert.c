extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0;
    while (x < 10000000) {
        ++x;
        __VERIFIER_assert(x < 50);
        if (x == 100) break;
    }
    return 0;
}
