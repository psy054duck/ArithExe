extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void reach_error(void) { abort(); }
int main(void) {
    int a[3];
    for (int i=0;i<3;++i) a[i]=__VERIFIER_nondet_int();
    int m=-10;
    for (int i=0;i<3;++i) if (a[i]>m) m=a[i];
    for (int i=0;i<3;++i) if (m<a[i]) reach_error();
    return 0;
}
