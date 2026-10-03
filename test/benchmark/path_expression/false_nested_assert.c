#include "../loops/assert.h"
int main() {
    unsigned w, z, v;
    for (w = 0; w < 100000000; ++w)
        for (z = 0; z < 10; ++z) {
            for (v = 0; v < 10; ++v);
            __VERIFIER_assert(v % 5);
        }
    return 0;
}
