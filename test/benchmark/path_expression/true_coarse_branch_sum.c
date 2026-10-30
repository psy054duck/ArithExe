extern _Bool __VERIFIER_nondet_bool(void);
extern unsigned int __VERIFIER_nondet_uint(void);
extern void __VERIFIER_assert(int);

int main(void) {
    unsigned int n = __VERIFIER_nondet_uint();
    if (n > 100000000) return 0;
    unsigned int x = 0, y = 0, i = 0;
    while (i < n) {
        if (__VERIFIER_nondet_bool()) x++;
        else y++;
        i++;
    }
    __VERIFIER_assert(x + y == i);
    return 0;
}
