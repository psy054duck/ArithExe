#include "../loops/assert.h"
int main() {
    int i, j = 7;
    for (i = 9; i > 0; i -= 2)
        for (j = 2; j >= 0; --j);
    __VERIFIER_assert(i == -1);
    __VERIFIER_assert(j == -1);
    return 0;
}
