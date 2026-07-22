extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);

__attribute__((noinline)) void fail_positive(void) {
    reach_error();
}

__attribute__((noinline)) void fail_nonpositive(void) {
    reach_error();
}

__attribute__((noinline)) int unsafe_entry(void) {
    int value = __VERIFIER_nondet_int();
    if (value > 0) {
        fail_positive();
    } else {
        fail_nonpositive();
    }
    return 0;
}

__attribute__((noinline)) int safe_entry(void) {
    return 0;
}

int main(void) {
    return unsafe_entry();
}
