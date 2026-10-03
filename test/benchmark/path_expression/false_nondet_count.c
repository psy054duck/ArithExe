#include "../loops/assert.h"
extern _Bool __VERIFIER_nondet_bool(void);

int main() {
    int x = 0;
#ifdef PATH_EXPRESSION_EXIT_ON_TRUE
    while (!__VERIFIER_nondet_bool()) x++;
#else
    while (__VERIFIER_nondet_bool()) x++;
#endif
    // The shortest violation needs acceleration after the observed iteration.
    __VERIFIER_assert(x <= 1);
    return 0;
}
