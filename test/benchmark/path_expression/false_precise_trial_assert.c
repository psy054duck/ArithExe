extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0;
    while (x < 100) {
        x++;
        __VERIFIER_assert(x != 1);
    }
    return 0;
}
