// A "third-party" shared library that is NOT coverage instrumented, like the
// ROS2/cyclonedds .so files a FuzzTest binary loads. Its only interesting
// content is the crtbeginS.o/crtendS.o the toolchain links into every shared
// object: if those were compiled with the user's --copt they carry their own
// __sancov_cntrs section and module constructor.
int fuzztest_coverage_dep(int x) {
    return x * 2 + 1;
}
