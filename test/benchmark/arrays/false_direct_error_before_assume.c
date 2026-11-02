extern void __VERIFIER_error(void);
extern void __VERIFIER_assume(int condition);

int main(void) {
    for (int i = 0; i < 2; ++i) {
        __VERIFIER_error();
        __VERIFIER_assume(0);
    }
    return 0;
}
