extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error(void) __attribute__((noreturn));

int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n < 0) return 0;
    int i = 0;
    while (i < n) {
        ++i;
        if (i == 17) __VERIFIER_error();
    }
    return 0;
}
