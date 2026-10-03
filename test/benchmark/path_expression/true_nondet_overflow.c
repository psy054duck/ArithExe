#include "../loops/assert.h"
#include <limits.h>
extern _Bool __VERIFIER_nondet_bool(void);

int main() {
    int x = __VERIFIER_nondet_int();
    if (x != INT_MAX - 1) return 0;
    while (__VERIFIER_nondet_bool()) x++;
    // Only zero or one iteration is defined under C signed arithmetic.
    __VERIFIER_assert(x >= 0);
    return 0;
}
