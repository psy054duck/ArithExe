extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

long long reduce(int *a) {
    long long sum = 0;
    for (int i = 0; i < 40; ++i) sum += i + a[i];
    return RETURN_AVERAGE ? sum / 40 : sum;
}

int main(void) {
    int a[40];
    for (int i = 0; i < 40; ++i) a[i] = __VERIFIER_nondet_int();
    long long first = reduce(a);
    int temp = a[0];
    a[0] = a[1];
    a[1] = temp;
    long long second = reduce(a);
    temp = a[0];
    for (int i = 0; i < 39; ++i) a[i] = a[i+1];
    a[39] = temp;
    long long third = reduce(a);
    __VERIFIER_assert(first == second);
    __VERIFIER_assert(first == third + RETURN_OFFSET);
    return 0;
}
