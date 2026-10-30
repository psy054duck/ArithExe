extern int __VERIFIER_nondet_int();
extern void __VERIFIER_assert(int);
int spill, spill_copy, unused_choice;
int main(void) {
    unsigned int x = __VERIFIER_nondet_int();
outer:;
    int token = __VERIFIER_nondet_int();
    if (!token) return 0;
    if (x % 3 == 1) { x += 2; spill = token; goto inner; }
    if (x % 3 == 2) { x += 1; spill = token; goto inner; }
    unused_choice = __VERIFIER_nondet_int();
    x += unused_choice ? 4 : 5;
    goto outer;
inner:;
    token = spill;
    spill_copy = token;
    if (!__VERIFIER_nondet_int()) {
        __VERIFIER_assert(x % 3 == 0);
        return 0;
    }
    if (x % 3 == 1) { x += 2; spill = token; goto inner; }
    if (x % 3 == 2) { x += 1; spill = token; goto inner; }
    x += __VERIFIER_nondet_int() ? 4 : 5;
    goto outer;
}
