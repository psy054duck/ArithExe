extern void reach_error(void);

static void check(int condition) {
    if (!condition) reach_error();
}

int main(void) {
    volatile int raw = 256;
    unsigned char narrowed = (unsigned char)raw;
    check(narrowed == 0);

    raw = 255;
    signed char signed_value = (signed char)raw;
    unsigned char unsigned_value = (unsigned char)raw;
    check(signed_value == -1);
    check(unsigned_value == 255);

    return 0;
}
