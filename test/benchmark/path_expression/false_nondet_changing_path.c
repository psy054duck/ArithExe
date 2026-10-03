#include "../loops/assert.h"
extern _Bool __VERIFIER_nondet_bool(void);

int main() {
    int x = 0;
    int y = 0;
    while (__VERIFIER_nondet_bool()) {
        if (x < 2) y++;
        else y--;
        x++;
    }
    // Treating the initial y++ path as stable would miss this violation.
    __VERIFIER_assert(y >= 0);
    return 0;
}
