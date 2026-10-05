#  Overview

This repo reproduces, and carries patches for, a set of problems found when moving to the hermetic `@llvm` Bazel toolchain ([hermeticbuild/hermetic-llvm](https://github.com/hermeticbuild/hermetic-llvm) `0.8.24`, `glibc-2.34`): choosing the `fastbuild` optimization level, header shadowing of BCR packages, sanitizer runtime gaps, and three failure modes specific to fuzzing. Every patch is applied to `@llvm` through `single_version_override` in [MODULE.bazel](MODULE.bazel); each section below names the patch, the upstream PR, and the Bazel command that only succeeds with it.

| Patch | Upstream PR | Repro |
|---|---|---|
| [fastbuild_compile_flags.patch](patches/fastbuild_compile_flags.patch) | [#802](https://github.com/hermeticbuild/hermetic-llvm/pull/802) | `-O` level on `-c fastbuild` compiles |
| [header_search_paths.patch](patches/header_search_paths.patch) | [#806](https://github.com/hermeticbuild/hermetic-llvm/pull/806) | `//shadowing/...` |
| [msan_libcxx.patch](patches/msan_libcxx.patch) | [#803](https://github.com/hermeticbuild/hermetic-llvm/pull/803) | `--config msan //sanitizers:msan` |
| [stage0_object_reset.patch](patches/stage0_object_reset.patch) | [#815](https://github.com/hermeticbuild/hermetic-llvm/pull/815) | `--config fuzztest //fuzzing:fuzztest_coverage_test` |
| [llvm21_third_party_zstd.patch](patches/llvm21_third_party_zstd.patch) | [#805](https://github.com/hermeticbuild/hermetic-llvm/pull/805) | sanitizer symbolizer builds on LLVM 21 |
| [asan_runtime.patch](patches/asan_runtime.patch) | [#813](https://github.com/hermeticbuild/hermetic-llvm/pull/813) | `--config asan //fuzzing:foreign_cc_compiler_check` |
| [sanitizer_runtime_libunwind.patch](patches/sanitizer_runtime_libunwind.patch) | [#814](https://github.com/hermeticbuild/hermetic-llvm/pull/814) | `--config asan //fuzzing:foreign_cc_compiler_check` |
| [sanitizer_strip_foreign_source_paths.patch](patches/sanitizer_strip_foreign_source_paths.patch) | [#816](https://github.com/hermeticbuild/hermetic-llvm/pull/816) | `--config fuzztest //fuzzing:cargo_build_script_test` |

# Fastbuild optimization level (patches/fastbuild_compile_flags.patch, [#802](https://github.com/hermeticbuild/hermetic-llvm/pull/802))

`-c fastbuild` compiles at `-O0`. [fastbuild_compile_flags.patch](patches/fastbuild_compile_flags.patch) adds a `fastbuild` toolchain feature backed by a string flag so the level can be chosen globally, without a `--copt` that would also reach the runtimes:

```
# .bazelrc
common --@llvm//config:fastbuild_optimization_mode=O1   # O0 (default), O1, O2, O3, Og, Os, Oz
```

The flag only affects the `fastbuild` compilation mode; `opt` and `dbg` keep their existing features.

# Sanitizers (patches/msan_libcxx.patch [#803](https://github.com/hermeticbuild/hermetic-llvm/pull/803), patches/llvm21_third_party_zstd.patch [#805](https://github.com/hermeticbuild/hermetic-llvm/pull/805))

MSan requires an instrumented C++ standard library; `msan_libcxx.patch` builds `libc++`/`libc++abi` with MSan under `--@llvm//config:msan` by following the existing `inherit_asan` pattern. `llvm21_third_party_zstd.patch` backports `@llvm-project//third-party:llvm_enable_zstd` to LLVM 21.x so the sanitizer symbolizer transitions resolve. With both in place every sanitizer config builds:

```
bazel build --config asan  //sanitizers:asan
bazel build --config msan  //sanitizers:msan
bazel build --config rtsan //sanitizers:rtsan
bazel build --config tsan  //sanitizers:tsan
bazel build --config ubsan //sanitizers:ubsan
```

# Fuzzing fixes ([#813](https://github.com/hermeticbuild/hermetic-llvm/pull/813), [#814](https://github.com/hermeticbuild/hermetic-llvm/pull/814), [#816](https://github.com/hermeticbuild/hermetic-llvm/pull/816), [#815](https://github.com/hermeticbuild/hermetic-llvm/pull/815))

The `//fuzzing` package reproduces the three failure modes that stopped C++ fuzz targets from building or from getting coverage feedback under the hermetic toolchain. Each example only passes with the corresponding patch applied:

```
bazel build --config asan     //fuzzing:foreign_cc_compiler_check
bazel test  --config ubsan    --@llvm//config:sanitizer_strip_foreign_source_paths //fuzzing:cargo_build_script_test
bazel test  --config asan     --@llvm//config:sanitizer_strip_foreign_source_paths //fuzzing:cargo_build_script_test
bazel test  --config fuzztest //fuzzing:fuzztest_coverage_test
```

`--config fuzztest` in [.bazelrc](.bazelrc) mirrors the nightly fuzzing workflow (`--config=ubsan --config=fuzztest`: ASan + UBSan + `--dynamic_mode=off` + `-fsanitize-coverage=inline-8bit-counters,trace-cmp,pc-table`) plus the opt-in flag, so `bazel test --config fuzztest //fuzzing/...` exercises everything at once.

Two of the scenarios involve build systems that do **not** compile C through Bazel's C++ actions (CMake via `rules_foreign_cc`, cc-rs via `rules_rust`'s `cargo_build_script`). Both obtain `CFLAGS`/`LDFLAGS` from the toolchain through `cc_common.get_memory_inefficient_command_line` and invoke `clang` themselves. [fuzzing/defs.bzl](fuzzing/defs.bzl) replays that pattern with two tiny rules so neither CMake nor Cargo is needed.

### `cyclonedds` (CMake compiler check) — `asan_runtime.patch` ([#813](https://github.com/hermeticbuild/hermetic-llvm/pull/813)) + `sanitizer_runtime_libunwind.patch` ([#814](https://github.com/hermeticbuild/hermetic-llvm/pull/814))
* **Error**: `rules_foreign_cc` hands CMake the toolchain's flags; CMake first compiles and links a trivial **C** program. Nothing on a C link adds libc++/libc++abi, and the ASan runtime built by `@llvm` lacked `-fno-rtti -fno-exceptions`, so the link failed with `undefined symbol: _Unwind_Resume`, `__cxa_begin_catch`, `__gxx_personality_v0`, `std::terminate()` … CMake then declared the compiler broken and every fuzz target depending on `cyclonedds` disappeared.
* **Repro**: `//fuzzing:foreign_cc_compiler_check` runs `clang CFLAGS LDFLAGS check.c -o check` with the exact flags the toolchain emits for `c_compile` + `cpp_link_executable`.
* **Fix**: two independent patches, each leaving a disjoint set of undefined symbols when applied alone:
  * `asan_runtime.patch` (bug fix): apply the already-declared `ASAN_CFLAGS` / `UBSAN_STANDALONE_CFLAGS` (`-fno-rtti -fno-exceptions`, as upstream CMake does) to the `asan` and `ubsan_standalone` runtimes. Alone, the C link still misses `_Unwind_Backtrace` / `_Unwind_GetIP`.
  * `sanitizer_runtime_libunwind.patch` (toolchain policy): make `sanitizer_common_symbolizer` depend on `libunwind` on Linux, because the toolchain links with `--unwindlib=none` and only hands `libunwind` to C++ links via `static_runtime_lib`. Alone, the C link still misses `__cxa_begin_catch` / `__gxx_personality_v0` / `std::terminate()`.

### `ring` (cc-rs in `cargo_build_script`) — `sanitizer_strip_foreign_source_paths.patch` ([#816](https://github.com/hermeticbuild/hermetic-llvm/pull/816))
* **Error**: cc-rs compiles C with **absolute** source paths. With sanitizers now coming from the toolchain instead of `--copt`, UBSan source locations and ASan global metadata embedded `/…/execroot/_main/.../file.c`; neither `-ffile-prefix-map` nor `-fdebug-prefix-map` rewrites those strings (verified with the hermetic clang 21) and `rules_rust` rejected the result (`rlib embeds the absolute working directory`).
* **Repro**: `//fuzzing:cargo_build_script_cc_object` compiles `$(pwd)/fuzzing/cargo_build_script.c` with the toolchain's `c_compile` flags (no `source_file` variable, exactly like `rules_rust`). `//fuzzing:cargo_build_script_test` links that object and scans its own loaded `PT_LOAD` segments for `/cargo_build_script.c`; without the flag it prints the full execroot path.
* **Fix**: an **opt-in** `bool_flag`, `--@llvm//config:sanitizer_strip_foreign_source_paths` (default off). When set, compile command lines that carry no `source_file` variable — which only happens when another rule set extracts the toolchain's flags for a foreign build system (`rules_foreign_cc`, `rules_rust` `cargo_build_script`, `$(CC_FLAGS)`) — get `-fsanitize-undefined-strip-path-components=-1` under UBSan and `-mllvm -asan-globals=0` under ASan. Bazel's own compile actions always set `source_file` and use workspace-relative paths, so they are unaffected and keep full ASan global instrumentation and full UBSan report paths.

### FuzzTest coverage counters — `stage0_object_reset.patch` ([#815](https://github.com/hermeticbuild/hermetic-llvm/pull/815))
* **Error**: FuzzTest instruments with `--copt=-fsanitize-coverage=inline-8bit-counters,trace-cmp,pc-table`. `cc_stage0_object`'s bootstrap transition reset the sanitizer settings but not `--copt`, so `crtbegin.o`/`crtend.o` were instrumented too. Every shared library in the process then carried its own `__sancov_cntrs` section whose constructor ran *before* the executable's; FuzzTest kept the first range it saw and warned that `__sanitizer_cov_8bit_counters_init` was called multiple times — coverage dropped from ~550k counters to 7.
* **Repro**: `//fuzzing:fuzztest_coverage_test` defines `__sanitizer_cov_8bit_counters_init` like FuzzTest does and loads an uninstrumented `.so`. Without the patch it reports `7 counters  …/libfuzztest_coverage_dep.so  <-- unexpected`.
* **Fix**: clear `copt`/`cxxopt`/`conlyopt`/`linkopt` (and `host_*`) in the `cc_stage0_object` transition, as `configure_builder_for_runtimes()` already does for the other runtimes, and also reset the `//config:fuzzer` / `//config:profile` settings it had missed (so `--@llvm//config:fuzzer` cannot instrument `crtbegin.o` either).

# Header shadowing fix (patches/header_search_paths.patch, [#806](https://github.com/hermeticbuild/hermetic-llvm/pull/806))

[header_search_paths.patch](patches/header_search_paths.patch) fixes header shadowing across the workspace by moving the `@llvm` toolchain's system headers out of the user `-isystem` search bucket (which Bazel places on the command line **before** target `includes` and `copts`) into Clang's internal system header buckets:

* **`libc++` / `libc++abi`**: `-isystem` $\rightarrow$ `-stdlib++-isystem`
* **`glibc` & Linux `kernel_headers`**: `-isystem` $\rightarrow$ `-Xclang -internal-isystem -Xclang <path>`
* **Clang builtin headers (`llvm@0.8.21+`)**: `-resource-dir=<dir>` $\rightarrow$ `-Xclang -internal-isystem -Xclang <include_dir>`

With the patches in place, these modules no longer have the header shadowing issues they did previously:

```
bazel build //shadowing/ffmpeg:example
bazel build //shadowing/osqp:example
bazel build //shadowing/rules_cuda:example
bazel build //shadowing/systemd:example
```

### `osqp`
* **Shadowing error**: `osqp` provides its own `include/error.h` via `cc_library.includes` (which Bazel passes as `-isystem`). Because `@llvm` previously passed `glibc` headers via `-isystem` *ahead* of target `includes`, `glibc`'s `<error.h>` shadowed `osqp`'s `error.h` when `osqp` sources included it.
* **How it was fixed**: Moving `glibc` to `-internal-isystem` places it after target `-isystem` (`includes`), so `osqp`'s `include/error.h` is resolved first without needing `-iquote external/osqp+/include`.

### `systemd`
* **Shadowing error**: `systemd` puts `src/include/override` and `src/include/uapi` (containing override headers like `linux/capability.h` and `sys/mount.h` that use `#include_next`) into `includes` (`-isystem`). Because the `@llvm` toolchain's `-isystem` flags for `glibc` and `kernel_headers` appeared earlier on the command line, the toolchain headers shadowed `systemd`'s override/UAPI headers during both compilation and macro table generation (`codegen.bzl`).
* **How it was fixed**: Moving `glibc` and `kernel_headers` to `-internal-isystem` ensures `systemd`'s `-isystem` directories are searched first and can then `#include_next` into the toolchain's `glibc`/kernel headers.

### `rules_cuda`
* **Shadowing error**: When compiling CUDA code with Clang (`--@rules_cuda//cuda:compiler=clang`), Clang automatically injects `<resource-dir>/include/cuda_wrappers` as an internal system include so its device-compatible wrappers (e.g., `cuda_wrappers/new`, `cuda_wrappers/complex`, `cuda_wrappers/algorithm`) wrap and `#include_next` the standard library headers. Because `@llvm` passed `libc++` as a plain user `-isystem`, `libc++`'s host-only `<new>` was searched *before* Clang's internal `cuda_wrappers/new`, shadowing the CUDA wrapper and breaking device compilation.
* **How it was fixed**: Passing `libc++` via `-stdlib++-isystem` places it in Clang's C++ standard library search slot (after Clang's `cuda_wrappers` directory), allowing `cuda_wrappers/new` to take precedence and `#include_next` into `libc++`.

### `ffmpeg`
* **Search path error**: Starting in `llvm@0.8.21`, the toolchain passed `-resource-dir=...` at compile time. Clang ignores `-resource-dir=` inside `--config` files used by `aspect_rules_lint`'s `clang_tidy` runner (causing builtin headers like `<stddef.h>` to go missing), and `-resource-dir` also broke `ffmpeg` builds.
* **How it was fixed**: Replacing compile-time `-resource-dir=<dir>` with `-Xclang -internal-isystem -Xclang <builtin_include_dir>` explicitly appends the Clang builtin headers to the internal system include path, which works inside `--config` files and does not break `ffmpeg`.
