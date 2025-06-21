
/* extended Euclid's algorithm */

// This file is part of the SV-Benchmarks collection of verification tasks:
// https://gitlab.com/sosy-lab/benchmarking/sv-benchmarks
//
// It was automatically generated from 'egcd-ll.c' with https://github.com/FlorianDyck/semtransforms
// To reproduce it you can use the following command:
// python run_transformations.py [insert path here]egcd-ll.c -o . --pretty_names --trace to_recursive:0
// in case the newest version cannot recreate this file, the commit hash of the used version is 869b5a9

extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *) __attribute__((__nothrow__, __leaf__)) __attribute__((__noreturn__));
void reach_error()
{
  __assert_fail("0", "egcd-ll.c", 4, "reach_error");
}

extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void assume_abort_if_not(int cond)
{
  if (!cond)
  {
    abort();
  }
  else
  {
  }
}

void __VERIFIER_assert(int cond)
{
  if (!cond)
  {
    ERROR:
    {
      reach_error();
    }

  }
  else
  {
  }
  return;
}


void change(int* a, int* b) {
    int tmp;
    tmp = *a;
    *a = *b;
    *b = tmp;
}

int main()
{
  long long a;
  long long b;
  long long p;
  long long q;
  long long r;
  long long s;
  int x;
  int y;
  x = __VERIFIER_nondet_int();
  y = __VERIFIER_nondet_int();
  int x0 = x;
  int y0 = y;
  change(&x, &y);
  __VERIFIER_assert(y0 == y);
  // __VERIFIER_assert((((p * x) + (r * y)) - b) == 0);
  // __VERIFIER_assert((((q * r) - (p * s)) + 1) == 0);
  // __VERIFIER_assert((((q * x) + (s * y)) - b) == 0);
  return 0;
}
