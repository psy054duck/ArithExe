extern void __VERIFIER_assert(int);
int main(void) {
    int x = 0, y = 0;
    while (x < 4) {
        if (x % 2 == 0) y++;
        x++;
    }
    // A union-star forgets the alternating word; exact execution recovers it.
    __VERIFIER_assert(x + y == 6);
    return 0;
}
