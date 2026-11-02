extern void __VERIFIER_assert(int);

long long identity(long long value) { return value; }
long long shifted(int value) { return 4294967296LL + value; }

int main(void) {
    long long first = identity(4294967296LL);
    long long second = identity(0);
    long long third = shifted(0);
    long long fourth = shifted(0);
    __VERIFIER_assert(first == second || third != fourth);
    return 0;
}
