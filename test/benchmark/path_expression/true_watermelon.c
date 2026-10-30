extern void abort(void);
extern unsigned int __VERIFIER_nondet_uint(void);
void __VERIFIER_assert(int condition) { if (!condition) abort(); }
int true = 1;
int false = 0;
int correct_version(int w) {
    int result = true;
    if (w < 4) result = false;
    else {
        int i;
        for (i = 0; i < w; i += 2) {}
        if (i != w) result = false;
    }
    return result;
}
int student_version(int w) {
    int result = true;
    if (w < 4) result = false;
    else {
        int i;
        for (i = 0; i < w; i += 2) {}
        if (i != w) result = false;
    }
    return result;
}
#ifndef EXPECT_MATCH
#define EXPECT_MATCH 1
#endif
int main(void) {
    unsigned int w = __VERIFIER_nondet_uint();
    if (w > 0 && w < 10000000) {
        int a = correct_version(w);
        int b = student_version(w);
        __VERIFIER_assert((a == b) == EXPECT_MATCH);
    }
    return 0;
}
