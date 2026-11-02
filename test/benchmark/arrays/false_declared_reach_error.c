extern void reach_error(void) __attribute__((noreturn));

int main(void) {
    for (int i = 0; i < 2; ++i) {
        if (i == 1) reach_error();
    }
    return 0;
}
