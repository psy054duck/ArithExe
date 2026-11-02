/* Adapted from Michael Tautschnig's 2015 SV-COMP relaxed-prefix benchmark.
 * SPDX-License-Identifier: Apache-2.0
 * https://www.apache.org/licenses/LICENSE-2.0
 */
extern int __VERIFIER_nondet_int(void);
extern unsigned long __VERIFIER_nondet_ulong(void);
extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

_Bool relaxed_prefix(int pat[], unsigned long pn, int a[], unsigned long an) {
    if (pn>an+1) return 0;
    int shift=0;
    for (int i=0; i<pn && i<an; ++i) {
        if (pat[i]!=a[i-shift]) {
            if (shift==0) shift=1;
            else return 0;
        }
    }
    return !(pn>an && shift==1);
}

int main(void) {
    int pat[2], a[2];
    for (int i=0; i<2; ++i) pat[i]=__VERIFIER_nondet_int();
    for (int i=0; i<2; ++i) a[i]=__VERIFIER_nondet_int();
    if (relaxed_prefix(pat,2,a,2)) {
        unsigned long different=__VERIFIER_nondet_ulong();
        for (int i=0; i<2; ++i) {
            if (i<different) __VERIFIER_assume(pat[i]==a[i]);
            else if (i==different) __VERIFIER_assume(pat[i]!=a[i]);
            else __VERIFIER_assert(pat[i]==a[i-RELAXED_ASSERT_SHIFT]);
        }
    }
    return 0;
}
