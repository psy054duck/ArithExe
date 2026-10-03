#include "../loops/assert.h"
int main() {
    unsigned s = 0;
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned j = 0; j < 4; ++j)
            if (j % 2) ++s; else s += 2;
    __VERIFIER_assert(s == 12);
    return 0;
}
