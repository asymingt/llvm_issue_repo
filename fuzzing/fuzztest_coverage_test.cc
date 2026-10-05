// Minimal model of how FuzzTest/Centipede consume SanitizerCoverage.
//
// Build with --copt=-fsanitize-coverage=inline-8bit-counters (see the
// `fuzztest` config in .bazelrc). The compiler emits a module constructor per
// instrumented object that calls __sanitizer_cov_8bit_counters_init(start,
// stop) with the bounds of the __sancov_cntrs section of the *module* (main
// executable or shared library) it was linked into. FuzzTest only keeps the
// first range it is told about and warns when it is called again with
// different arguments.
//
// crtbegin.o/crtend.o are linked into every shared library. If the toolchain
// compiles them with the user's --copt, every .so in the process (the
// uninstrumented :fuzztest_coverage_dep below, libc++.so, ...) gets its own
// tiny counters section and its constructor runs *before* the executable's.
// FuzzTest then latches onto a handful of counters (the "7 counters instead of
// 550k" symptom) and fuzzes blind.
#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int fuzztest_coverage_dep(int x);

struct CounterRange {
    std::string module;
    uint8_t *start;
    uint8_t *stop;
};

static std::vector<CounterRange> &ranges() {
    static std::vector<CounterRange> r;
    return r;
}

extern "C" void __sanitizer_cov_8bit_counters_init(uint8_t *start, uint8_t *stop) {
    // Deduplicate like libFuzzer does: each TU's constructor reports the same
    // section bounds for the module it lives in.
    for (const CounterRange &r : ranges()) {
        if (r.start == start && r.stop == stop) return;
    }
    Dl_info info{};
    std::string module = "<unknown>";
    if (dladdr(start, &info) && info.dli_fname) module = info.dli_fname;
    ranges().push_back({module, start, stop});
}

// The nightly config instruments with `inline-8bit-counters,trace-cmp,pc-table`.
// FuzzTest/Centipede implement all of these callbacks; we only care about the
// counters above, so the rest are no-ops that merely let the binary link.
extern "C" {
void __sanitizer_cov_pcs_init(const uintptr_t *, const uintptr_t *) {}
void __sanitizer_cov_trace_cmp1(uint8_t, uint8_t) {}
void __sanitizer_cov_trace_cmp2(uint16_t, uint16_t) {}
void __sanitizer_cov_trace_cmp4(uint32_t, uint32_t) {}
void __sanitizer_cov_trace_cmp8(uint64_t, uint64_t) {}
void __sanitizer_cov_trace_const_cmp1(uint8_t, uint8_t) {}
void __sanitizer_cov_trace_const_cmp2(uint16_t, uint16_t) {}
void __sanitizer_cov_trace_const_cmp4(uint32_t, uint32_t) {}
void __sanitizer_cov_trace_const_cmp8(uint64_t, uint64_t) {}
void __sanitizer_cov_trace_switch(uint64_t, uint64_t *) {}
}

int main() {
    // Touch the dependency so the dynamic linker definitely loads it.
    if (fuzztest_coverage_dep(1) != 3) return 2;

    Dl_info self{};
    dladdr(reinterpret_cast<void *>(&main), &self);

    int failures = 0;
    std::printf("%zu distinct 8-bit counter range(s) registered:\n", ranges().size());
    for (const CounterRange &r : ranges()) {
        const bool is_main_executable =
            self.dli_fname && r.module == self.dli_fname;
        std::printf("  %6zu counters  %s%s\n", static_cast<size_t>(r.stop - r.start), r.module.c_str(),
                    is_main_executable ? "  (this executable)" : "  <-- unexpected");
        if (!is_main_executable) failures++;
    }

    if (ranges().empty()) {
        std::fprintf(stderr, "FAIL: no coverage counters at all; was -fsanitize-coverage passed?\n");
        return 1;
    }
    if (failures) {
        std::fprintf(stderr,
                     "FAIL: %d module(s) other than the test executable registered coverage counters.\n"
                     "FuzzTest would keep only the first range it sees and lose coverage guidance.\n",
                     failures);
        return 1;
    }
    std::puts("OK: only the test executable is instrumented");
    return 0;
}
