#include <limits.h>
extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void reach_error(void) { abort(); }
static long long reduce(const int x[], int length) {
    long long result=0;
    for (int i=0;i<length;++i) result+=x[i];
    return result;
}
int main(void) {
    int n=DUP_LENGTH;
    if (n<=1 || n>INT_MAX/2) return 0;
    int x[n], y[2*n];
    for (int i=0;i<n;++i) x[i]=__VERIFIER_nondet_int();
    for (int i=0;i<n;++i) { y[i]=x[i]; y[n+i]=x[i]; }
    long long a=reduce(x,n), b=reduce(y,2*n);
    if (b!=2*a+DUP_DELTA) reach_error();
    return 0;
}
