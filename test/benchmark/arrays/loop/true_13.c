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
int main ( ) {
  int a [100000];
  int b [100000];
  int i = 0;
  while ( i < 100000/2 ) {
    a[2*i + 1] = 1;
    a[2*i] = 0;
    i = i + 1;
  }
  for ( i = 0 ; i < 100000 ; i++ ) {
    if (i % 2 == 0) {
        b[i] = 0;
    } else {
        b[i] = 1;
    }
  }
  int x = 0;
  for ( x = 0 ; x < 100000 ; x++ ) {
    __VERIFIER_assert( a[x] == b[x] );
  }
  return 0;
}