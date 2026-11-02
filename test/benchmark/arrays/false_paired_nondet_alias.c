extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));

void __VERIFIER_assert(int condition) {
    if (!condition) __VERIFIER_error();
}

int ReadFromPort(void) {
    return __VERIFIER_nondet_int();
}

int main(void) {
    int a[8];
    for (int i = 0; i < 4; ++i) {
        int x = ReadFromPort();
        int y = ReadFromPort();
        a[2*i] = x != 0 ? x : 10;
        a[2*i+1] = y != 0 ? y : 10;
    }
    for (int i = 0; i < 4; ++i) {
        __VERIFIER_assert(a[2*i] == a[2*i+1]);
    }
    return 0;
}
