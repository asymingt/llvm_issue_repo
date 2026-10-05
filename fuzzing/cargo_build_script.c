// Stand-in for the C code inside the `ring` crate that cc-rs compiles from a
// cargo_build_script. It is compiled with an *absolute* source path by
// //fuzzing:cargo_build_script_cc_object.
//
// Both constructs below make a sanitizer record the source location:
//   * the global is described by ASan's __asan_global metadata
//     (module name + file:line:col),
//   * the signed addition is a UBSan check with a __ubsan_source_location.
// Without the patch the recorded file name is the absolute execroot path.
int cargo_build_script_global = 0;

int cargo_build_script_add(int a, int b) {
    cargo_build_script_global++;
    return a + b;
}
