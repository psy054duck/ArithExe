#include "../loops/assert.h"
int main() {
    int w, v;
    for (w = 0; w < 3; ++w)
        for (v = 0; v < 3; ++v)
            __VERIFIER_assert(w + v < 3);
    return 0;
}
