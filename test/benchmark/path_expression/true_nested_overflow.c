#include "../loops/assert.h"
int main() {
    int x = 2147483647;
    for (int i = 0; i < 1; ++i)
        for (int j = 0; j < 1; ++j)
            ++x;
    __VERIFIER_assert(x < 0);
    return 0;
}
