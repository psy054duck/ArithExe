#include "../loops/assert.h"
extern _Bool __VERIFIER_nondet_bool(void);

int main() {
    int x = 0;
    while (__VERIFIER_nondet_bool()) x++;
    __VERIFIER_assert(x > 0);
    return 0;
}
