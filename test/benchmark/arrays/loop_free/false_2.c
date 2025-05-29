/*
 * Benchmarks contributed by Divyesh Unadkat[1,2], Supratik Chakraborty[1], Ashutosh Gupta[1]
 * [1] Indian Institute of Technology Bombay, Mumbai
 * [2] TCS Innovation labs, Pune
 *
 */

extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *) __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));
void reach_error() { __assert_fail("0", "brs2.c", 10, "reach_error"); }
extern void abort(void);
void assume_abort_if_not(int cond) {
  if(!cond) {abort();}
}
void __VERIFIER_assert(int cond) { if(!(cond)) { ERROR: {reach_error();abort();} } }
extern int __VERIFIER_nondet_int(void);
void *malloc(unsigned int size);

int N = 5;

int main()
{
	long long sum[1];
	int *a = malloc(sizeof(int)*N);

	  int i = __VERIFIER_nondet_int() % N;
    int j = __VERIFIER_nondet_int() % N;
    a[0] = 0;
    a[1] = 1;
    a[2] = 2;
    a[2] = 6;
    a[3] = 3;
    a[4] = 4;
    a[i] = 3;

    __VERIFIER_assert(a[2] == 2);

	return 1;
}

