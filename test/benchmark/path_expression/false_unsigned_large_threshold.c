extern void __VERIFIER_assert(int);
int main(void) {
    unsigned int x = 0;
    while (x < 100000000) {
        if (x < 10000000) x++;
        else x += 2;
    }
    __VERIFIER_assert(x == 100000001);
    return 0;
}
