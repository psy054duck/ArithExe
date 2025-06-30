extern void abort(void);

extern void __assert_fail (const char *__assertion, const char *__file,
      unsigned int __line, const char *__function)
     __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));
extern void __assert_perror_fail (int __errnum, const char *__file,
      unsigned int __line, const char *__function)
     __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));
extern void __assert (const char *__assertion, const char *__file, int __line)
     __attribute__ ((__nothrow__ , __leaf__)) __attribute__ ((__noreturn__));

void reach_error() { ((void) sizeof ((0) ? 1 : 0), __extension__ ({ if (0) ; else __assert_fail ("0", "standard_copyInitSum2_ground-2.c", 3, __extension__ __PRETTY_FUNCTION__); })); }
void __VERIFIER_assert(int cond) { if(!(cond)) { ERROR: {reach_error();abort();} } }
extern int __VERIFIER_nondet_int();
void* malloc(unsigned int size);
int main ( ) {
  int N1 = __VERIFIER_nondet_int();
  int N2 = __VERIFIER_nondet_int();
  if (N1 <= 0 || N2 <= 0) {
    return 0;
  }
  if (2*N1 < N2) return 0;
  int* a = malloc(2*N1*sizeof(int));
  int* b = malloc(N2*sizeof(int));
  int i = 0;
  while ( i < N1 ) {
    a[2*i + 1] = 1;
    a[2*i] = 0;
    i = i + 1;
  }
  for ( i = 0 ; i < N2; i++ ) {
    if (i % 2 == 0) {
        b[i] = 0;
    } else {
        b[i] = 1;
    }
  }
  int x = 0;
  for ( x = 0 ; x < N2; x++ ) {
    __VERIFIER_assert( a[x] == b[x] );
  }
  return 0;
}