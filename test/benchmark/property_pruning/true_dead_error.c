extern int __VERIFIER_nondet_int(void);
extern _Bool __VERIFIER_nondet_bool(void);
extern void reach_error(void);
int main(void) {
    int n = __VERIFIER_nondet_int();
    if (n > 0) {
        if (!(n > 0)) reach_error();
    }
    while (__VERIFIER_nondet_bool()) {}
    return 0;
}
