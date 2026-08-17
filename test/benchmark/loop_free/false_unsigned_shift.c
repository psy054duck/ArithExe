extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);

int main(void) {
    int raw = __VERIFIER_nondet_int();
    if (raw == -1) {
        unsigned int value = (unsigned int)raw;
        if ((value >> 1) == 0) {
            return 0;
        }
        reach_error();
    }
    return 0;
}
