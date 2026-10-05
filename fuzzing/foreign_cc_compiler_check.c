// Stand-in for CMake's compiler sanity check (CMakeTestCCompiler / try_compile).
//
// CMake compiles and links this before configuring cyclonedds. It is plain C:
// nothing on the link line pulls in libc++/libc++abi. If the ASan runtime was
// built without -fno-rtti -fno-exceptions it references __cxa_begin_catch,
// __gxx_personality_v0 and _Unwind_Resume, this link fails, CMake declares the
// compiler broken, and every fuzz target depending on cyclonedds is skipped.
#include <stdio.h>

int main(void) {
    puts("compiler check ok");
    return 0;
}
