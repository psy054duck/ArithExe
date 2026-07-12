#include "assert.h"

int main() {
    int i = 0;
    int x = 0;

    while (i < 5) {
        int j = 0;
        while (j < i) {
            x = x + 1;
            j = j + 1;
        }
        i = i + 1;
    }

    __VERIFIER_assert(x == 10);
    return 0;
}
