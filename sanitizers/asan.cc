#include <iostream>

int main() {
    int* ptr = new int[10];
    ptr[0] = 42;

    delete[] ptr; // Memory is freed here

    // ERROR: Modifying memory that has already been deallocated
    ptr[0] = 99;  

    std::cout << "Value: " << ptr[0] << std::endl;
    return 0;
}