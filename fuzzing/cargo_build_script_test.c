// Checks that the object produced by //fuzzing:cargo_build_script_cc_object
// does not embed the absolute build directory, which is what rules_rust
// enforces ("rlib embeds the absolute working directory /b/f/w").
//
// Rather than parsing ELF we link the object into this test and scan the
// loaded PT_LOAD segments of our own executable for "/cargo_build_script.c".
// Only sanitizer metadata (ASan globals, UBSan source locations) lives in
// loaded segments; DWARF and the symbol table are not loaded, so they cannot
// cause false positives.
#define _GNU_SOURCE
#include <link.h>
#include <stdio.h>
#include <string.h>

int cargo_build_script_add(int a, int b);

struct scan_state {
    const char *needle;
    size_t needle_len;
    const char *hit;
};

// Reading ASan global redzones from instrumented code would trip ASan itself,
// and so would libc's (intercepted) memcmp, hence the hand-rolled compare.
__attribute__((no_sanitize("address", "undefined", "memory", "thread")))
static int bytes_equal(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

__attribute__((no_sanitize("address", "undefined", "memory", "thread")))
static int scan_segments(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    struct scan_state *st = data;
    // The first entry reported by dl_iterate_phdr is the main executable.
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        const char *base = (const char *)(info->dlpi_addr + ph->p_vaddr);
        size_t len = ph->p_memsz;
        if (len < st->needle_len) continue;
        for (size_t off = 0; off + st->needle_len <= len; off++) {
            if (bytes_equal(base + off, st->needle, st->needle_len)) {
                // Walk back to the start of the C string for the report.
                const char *start = base + off;
                while (start > base && start[-1] != '\0') start--;
                st->hit = start;
                return 1;
            }
        }
    }
    return 1;  // Stop after the main executable.
}

int main(void) {
    // Build the needle at run time so this literal is not itself embedded.
    static const char obfuscated[] = {
        '/' ^ 0x5a, 'c' ^ 0x5a, 'a' ^ 0x5a, 'r' ^ 0x5a, 'g' ^ 0x5a, 'o' ^ 0x5a,
        '_' ^ 0x5a, 'b' ^ 0x5a, 'u' ^ 0x5a, 'i' ^ 0x5a, 'l' ^ 0x5a, 'd' ^ 0x5a,
        '_' ^ 0x5a, 's' ^ 0x5a, 'c' ^ 0x5a, 'r' ^ 0x5a, 'i' ^ 0x5a, 'p' ^ 0x5a,
        't' ^ 0x5a, '.' ^ 0x5a, 'c' ^ 0x5a,
    };
    char needle[sizeof(obfuscated) + 1];
    for (size_t i = 0; i < sizeof(obfuscated); i++) needle[i] = (char)(obfuscated[i] ^ 0x5a);
    needle[sizeof(obfuscated)] = '\0';

    // Make sure the object is really linked in.
    if (cargo_build_script_add(1, 2) != 3) return 2;

    struct scan_state st = {needle, strlen(needle), NULL};
    dl_iterate_phdr(scan_segments, &st);
    if (st.hit) {
        fprintf(stderr,
                "FAIL: sanitizer metadata embeds a directory-qualified source path:\n"
                "  %s\n"
                "rules_rust would reject this artifact (\"rlib embeds the absolute working directory\").\n",
                st.hit);
        return 1;
    }
    puts("OK: no directory components in embedded source paths");
    return 0;
}
