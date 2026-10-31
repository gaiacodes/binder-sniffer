#include "bs_decoder.h"
#include <string.h>

void bs_parcel_init(bs_parcel *p, const uint8_t *data, size_t size) {
    p->data = data;
    p->size = size;
    p->pos  = 0;
    p->error = 0;
}

// Ensure n more bytes are available; latch error and refuse the read if not.
static int ensure(bs_parcel *p, size_t n) {
    if (p->error) return 0;
    if (p->pos + n > p->size) { p->error = 1; return 0; }
    return 1;
}

int32_t bs_read_i32(bs_parcel *p) {
    if (!ensure(p, 4)) return 0;
    int32_t v;
    memcpy(&v, p->data + p->pos, 4);
    p->pos += 4;
    return v;
}

int64_t bs_read_i64(bs_parcel *p) {
    if (!ensure(p, 8)) return 0;
    int64_t v;
    memcpy(&v, p->data + p->pos, 8);
    p->pos += 8;
    return v;
}

// Parcel entries are padded to 4-byte boundaries.
void bs_align4(bs_parcel *p) {
    size_t rem = p->pos & 3u;
    if (rem) p->pos += (4 - rem);
}

int bs_read_string16(bs_parcel *p, char *out, size_t outcap) {
    int32_t len = bs_read_i32(p);          // code units, or -1 for a null string
    if (p->error) return 1;
    if (len < 0) { if (outcap) out[0] = 0; return 0; }
    // On the wire: len code units followed by a null terminator, padded to 4.
    // Compute the byte count in size_t: `len + 1` in int would be signed
    // overflow (UB) for a malformed length near INT32_MAX.
    size_t nbytes = ((size_t)(uint32_t)len + 1) * 2;
    if (!ensure(p, nbytes)) return 1;
    size_t o = 0;
    for (int32_t i = 0; i < len; i++) {
        uint16_t c;
        memcpy(&c, p->data + p->pos + (size_t)i * 2, 2);
        if (o + 1 < outcap) out[o++] = (c < 0x80) ? (char)c : '?';
    }
    if (outcap) out[o] = 0;
    p->pos += nbytes;                      // consume chars + null terminator
    bs_align4(p);
    return 0;
}

void bs_skip_string16(bs_parcel *p) {
    int32_t len = bs_read_i32(p);
    if (p->error || len < 0) return;
    size_t nbytes = ((size_t)(uint32_t)len + 1) * 2;
    if (!ensure(p, nbytes)) return;   // latch error if the string runs past the buffer
    p->pos += nbytes;
    bs_align4(p);
}

int bs_match_utf16_at(const uint8_t *buf, size_t len, size_t pos,
                      const char *needle, int require_terminator) {
    size_t n = strlen(needle);
    size_t need = n * 2 + (require_terminator ? 2u : 0u);
    if (pos > len || need > len - pos) return 0;   // overflow-safe bounds check
    for (size_t j = 0; j < n; j++) {
        if (buf[pos + j * 2] != (uint8_t)needle[j] || buf[pos + j * 2 + 1] != 0)
            return 0;
    }
    if (require_terminator && (buf[pos + n * 2] != 0 || buf[pos + n * 2 + 1] != 0))
        return 0;
    return 1;
}

long bs_find_utf16(const uint8_t *buf, size_t len, const char *needle,
                   int require_terminator) {
    for (size_t i = 0; i < len; i++) {
        if (bs_match_utf16_at(buf, len, i, needle, require_terminator))
            return (long)i;
    }
    return -1;
}
