#include "../loops/assert.h"

int main() {
    int x = 0;
    int y = 0;
    while (x < 3) {
        y = __VERIFIER_nondet_int();
        x++;
    }
    __VERIFIER_assert(x == 3);
    return 0;
}
