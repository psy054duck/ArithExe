int x, y;
volatile int v;
int update(int a) { int old = x; x = x + a; y = x - y; return old; }
void volatile_update(void) { --v; }
void pointer_update(int *p) { --*p; }
void conditional_update(int a) { if (a) --x; else ++x; }
int main(void) { return 0; }
