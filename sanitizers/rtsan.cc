#include <vector>
#include <iostream>

// Mark the function as a deterministic, real-time context
void process_audio() [[clang::nonblocking]] {
    // VIOLATION: std::vector push_back triggers a heap allocation (malloc)
    std::vector<int> buffer;
    buffer.push_back(42); 
}

int main() {
    std::cout << "Starting real-time processing...\n";
    process_audio();
    std::cout << "Processing complete!\n";
    return 0;
}
