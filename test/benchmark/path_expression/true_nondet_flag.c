#include "../loops/assert.h"
extern _Bool __VERIFIER_nondet_bool(void);

int main() {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    int i = __VERIFIER_nondet_int();
    int j = __VERIFIER_nondet_int();
    _Bool flag = __VERIFIER_nondet_bool();
    x = 0;
    y = 0;
    if (!(i == 0 && j == 0)) return 0;
    while (__VERIFIER_nondet_bool()) {
        x++;
        y++;
        i += x;
        j += y;
        if (flag) j += 1;
    }
#ifdef PATH_EXPRESSION_REQUIRE_EQUALITY
    __VERIFIER_assert(j == i);
#else
    __VERIFIER_assert(j >= i);
#endif
    return 0;
}
