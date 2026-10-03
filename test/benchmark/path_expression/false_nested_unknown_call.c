#include "../loops/assert.h"
extern int __VERIFIER_nondet_int(void);
int main() {
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            int value = __VERIFIER_nondet_int();
            __VERIFIER_assert(value == 0);
        }
    return 0;
}
