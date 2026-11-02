extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int average(int *a) {
    long long sum = 0;
    for (int i = 0; i < 5; ++i) sum += a[i];
    return sum / 5;
}

int main(void) {
    int a[5];
    for (int i = 0; i < 5; ++i) a[i] = __VERIFIER_nondet_int();
    int first = average(a);
    int temp = a[0];
    a[0] = a[1];
    a[1] = temp;
    int second = average(a);
    temp = a[0];
    for (int i = 0; i < 4; ++i) a[i] = a[i+1];
    a[4] = temp;
    int third = average(a);
    __VERIFIER_assert(first == second);
    __VERIFIER_assert(first == third + AVERAGE_OFFSET);
    return 0;
}
