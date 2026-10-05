#include <limits.h>

int main() {
    int max_val = INT_MAX; // 2147483647 for 32-bit int
    max_val += 1;          // This causes signed integer overflow (Undefined Behavior)
    return 0;
}