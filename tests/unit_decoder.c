// Unit tests for the decoder core. Built with -fsanitize=address,undefined
// (see `make unit`), so any out-of-bounds read or UB in the code under test
// fails the run, not just a wrong return value.
//
// Covers two things the fixture-based golden test (tests/run.sh) cannot:
//   1. the UTF-16 search primitives, including the NUL-terminator guard that
//      stops "android.text" matching inside "android.textLines";
//   2. that malformed / truncated / oversized input fails gracefully instead
//      of crashing or reading past the buffer.
#include "bs_decoder.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); fails++; } \
    else         { printf("  ok:   %s\n", #cond); } \
} while (0)

// Encode an ASCII string as UTF-16LE into out; return byte length. With
// with_nul, append a UTF-16 NUL terminator.
static size_t enc(uint8_t *out, const char *s, int with_nul) {
    size_t n = 0;
    for (; *s; s++) { out[n++] = (uint8_t)*s; out[n++] = 0; }
    if (with_nul) { out[n++] = 0; out[n++] = 0; }
    return n;
}

static void test_utf16_search(void) {
    printf("utf16 search:\n");
    uint8_t buf[256];

    size_t n = enc(buf, "android.text", 1);
    CHECK(bs_find_utf16(buf, n, "android.text", 1) == 0);           // exact + terminator

    n = enc(buf, "android.textLines", 1);
    CHECK(bs_find_utf16(buf, n, "android.text", 1) == -1);          // terminator guard
    CHECK(bs_find_utf16(buf, n, "android.text", 0) == 0);           // prefix search matches

    memset(buf, 0xAB, sizeof buf);
    size_t pre = enc(buf, "xx", 0);
    size_t m = enc(buf + pre, "android.title", 1);
    CHECK(bs_find_utf16(buf, pre + m, "android.title", 1) == (long)pre);  // offset inside buffer

    // Bounds: needle longer than the buffer must never read past it (ASan guards).
    CHECK(bs_find_utf16(buf, 3, "android.title", 1) == -1);
    CHECK(bs_match_utf16_at(buf, 4, 2, "android.title", 0) == 0);
    CHECK(bs_match_utf16_at(buf, 10, 100, "x", 0) == 0);            // pos past end
}

static void test_malformed_decode(void) {
    printf("malformed decode (must fail gracefully, never crash):\n");
    bs_notif out;

    CHECK(bs_decode_enqueue((const uint8_t *)"", 0, &out) != 0);    // empty -> too short to parse
    CHECK(bs_decode_enqueue((const uint8_t *)"\x01", 1, &out) != 0); // 1 byte -> too short

    // All-zero: a degenerate-but-parseable parcel (empty interface/pkg, no keys).
    // The decoder is lenient, so this "succeeds" — the safety property that
    // matters is that it never invents a title/text out of zero bytes.
    uint8_t zeros[64] = {0};
    (void)bs_decode_enqueue(zeros, sizeof zeros, &out);
    CHECK(out.have_title == 0 && out.have_text == 0);

    // A bogus String16 length of INT32_MAX must not overflow or over-read.
    uint8_t bogus[32] = {0};
    bogus[12] = 0xFF; bogus[13] = 0xFF; bogus[14] = 0xFF; bogus[15] = 0x7F;
    CHECK(bs_decode_enqueue(bogus, sizeof bogus, &out) != 0);

    // Non-zero garbage of an awkward (non-4-aligned) length.
    uint8_t garbage[37];
    for (size_t i = 0; i < sizeof garbage; i++) garbage[i] = (uint8_t)(i * 7 + 1);
    (void)bs_decode_enqueue(garbage, sizeof garbage, &out);         // just must not crash
    CHECK(1);
}

int main(void) {
    test_utf16_search();
    test_malformed_decode();
    printf(fails ? "\nRESULT: %d check(s) FAILED\n" : "\nRESULT: all checks passed\n", fails);
    return fails ? 1 : 0;
}
