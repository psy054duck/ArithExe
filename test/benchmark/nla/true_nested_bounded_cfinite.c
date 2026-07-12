#include "../loops/assert.h"

int main() {
    int i = 0;
    long long x = 2;
    long long y = 3;

    while (i < 4) {
        int j = 0;
        while (j < 2) {
            long long s = x + y;
            long long q = s * s;
            x = x + q;
            y = y - q;
            j = j + 1;
        }
        i = i + 1;
    }

    __VERIFIER_assert(x == 2 + 8 * 25);
    __VERIFIER_assert(y == 3 - 8 * 25);
    return 0;
}
