#include <iostream>
#include <string>

int main() {
    // Allocate an uninitialized integer on the stack
    int uninit_var;
    
    // MSan will catch this read of uninitialized memory
    int val = uninit_var + 5; 

    std::cout << "Value: " << val << std::endl;
    return 0;
}