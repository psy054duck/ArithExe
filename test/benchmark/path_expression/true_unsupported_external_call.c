#include "../loops/assert.h"
extern int external_function(void);

int main() {
    int x = 0;
    int y = 0;
    while (x < 3) {
        y = external_function();
        x++;
    }
    __VERIFIER_assert(x == 3);
    return 0;
}
