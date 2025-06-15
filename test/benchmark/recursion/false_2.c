extern int __VERIFIER_nondet_int();

#include "assert.h"
int id(int x) {
  if (x<0) return id(-x);
    if (x==0) return 0;
  return id(x-1) + 1;
}

int main(void) {
  int input = __VERIFIER_nondet_int();
  int result = id(input);
  __VERIFIER_assert(result == input);
}