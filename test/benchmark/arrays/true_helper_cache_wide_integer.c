extern void __VERIFIER_assert(int);

long long identity(long long value) { return value; }
long long shifted(int value) { return 4294967296LL + value; }
int small(int value) { return value + 1; }

int main(void) {
    __VERIFIER_assert(identity(4294967296LL) == 4294967296LL);
    __VERIFIER_assert(identity(0) == 0);
    __VERIFIER_assert(identity(-4294967296LL) == -4294967296LL);
    __VERIFIER_assert(shifted(0) == 4294967296LL);
    __VERIFIER_assert(shifted(0) == 4294967296LL);
    __VERIFIER_assert(small(4) == 5);
    __VERIFIER_assert(small(4) == 5);
    return 0;
}
