int leaf(int x) { if (x < 10) return x + 2; return x + 4; }
int nested(int x) { return leaf(x) + 1; }
volatile int global;
int effect(int x) { global = x; return x; }
int recursive(int x) { if (x == 0) return 0; return recursive(x - 1) + 1; }
int main(void) { return 0; }
