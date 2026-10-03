#include "../loops/assert.h"
extern int __VERIFIER_nondet_int(void);
int main() {
    int n = __VERIFIER_nondet_int();
    if (n < -1 || n > 3) return 0;
    int i = 0, j = 7;
    for (i = 0; i < n; ++i)
        for (j = 0; j < 2; ++j);
    __VERIFIER_assert(i == (n > 0 ? n : 0));
    __VERIFIER_assert(j == (n > 0 ? 2 : 7));
    return 0;
}
