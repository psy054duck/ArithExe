extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assert(int);

int size;

int mapped_average(int *a) {
    long long sum = 0;
    for (int i = 0; i < size; ++i) sum += i + a[i];
    return sum / size;
}

int main(void) {
    size = __VERIFIER_nondet_int();
    if (size > 1) {
        int a[size];
        for (int i = 0; i < size; ++i) a[i] = __VERIFIER_nondet_int();
        int first = mapped_average(a);
        int temp = a[0];
        a[0] = a[1];
        a[1] = DUPLICATE_ELEMENT ? a[1] : temp;
        int second = mapped_average(a);
        temp = a[0];
        for (int i = 0; i < size - 1; ++i) a[i] = a[i+1];
        a[size-1] = temp;
        int third = mapped_average(a);
        __VERIFIER_assert(first == second);
        __VERIFIER_assert(first == third + PERMUTATION_OFFSET);
    }
    return 0;
}
