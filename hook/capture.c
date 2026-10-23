#include "capture.h"
#include "bs_decoder.h"  // bs_decode_enqueue, bs_notif

#include <stdint.h>      // uint32_t, uint8_t, uintptr_t
#include <string.h>      // strlen
#include <android/log.h> // __android_log_print

// The interface we care about. Its descriptor sits near the start of the
// transaction's Parcel, stored as UTF-16LE.
#define WANT_IFACE "android.app.INotificationManager"

// A binder command code carries its payload size in bits 16..29 (the ioctl
// _IOC_SIZE field), so we can step over each command without a size table.
#define BC_PAYLOAD_SIZE(cmd) (((cmd) >> 16) & 0x3FFF)

// True if `buf` contains the ASCII string `s` encoded as UTF-16LE (each char
// as low byte + 0x00). Used to spot the interface token without a full parse.
static int contains_utf16(const uint8_t *buf, size_t len, const char *s) {
    size_t slen = strlen(s);
    if (len < slen * 2) return 0;
    for (size_t i = 0; i + slen * 2 <= len; i++) {
        size_t j = 0;
        while (j < slen && buf[i + j * 2] == (uint8_t)s[j] && buf[i + j * 2 + 1] == 0)
            j++;
        if (j == slen) return 1;
    }
    return 0;
}

// Does this transaction's Parcel target INotificationManager? The descriptor is
// right after the small header, so scanning the first chunk is enough.
static int is_notification(const uint8_t *parcel, size_t len) {
    if (!parcel) return 0;
    size_t scan = len < 256 ? len : 256;
    return contains_utf16(parcel, scan, WANT_IFACE);
}

// Decode one transaction's Parcel in place and log the notification text. The
// payload bytes live behind data.ptr.buffer, not inline in the command stream,
// so we follow it; it's in our own address space, valid for this ioctl call.
// The decoder is read-only and stateless, so this is safe on the binder thread.
static void log_notification(const struct binder_transaction_data *btd,
                             const uint8_t *parcel, size_t len) {
    bs_notif n;
    if (bs_decode_enqueue(parcel, len, &n) != 0) {
        __android_log_print(ANDROID_LOG_WARN, "binder-sniffer",
            "decode failed code=%u size=%zu", btd->code, len);
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, "binder-sniffer",
        "pkg=%s title=\"%s\" text=\"%s\"",
        n.pkg,
        n.have_title ? n.title : "(none)",
        n.have_text  ? n.text  : "(none)");
}

void bs_capture_bwr(const struct binder_write_read *bwr) {
    // Walk the command stream. Each command is a u32 code whose payload size is
    // encoded in the code itself, so we can step from one command to the next
    // without a per-command size table. For each transaction, decode and log it
    // only if it targets INotificationManager.
    uintptr_t base = (uintptr_t)bwr->write_buffer;
    size_t off = 0;
    while (off + 4 <= (size_t)bwr->write_size) {
        uint32_t cmd = *(uint32_t *)(base + off);
        uint32_t payload = BC_PAYLOAD_SIZE(cmd);
        off += 4;
        if (cmd == BC_TRANSACTION || cmd == BC_TRANSACTION_SG) {
            const struct binder_transaction_data *btd =
                (const struct binder_transaction_data *)(base + off);
            const uint8_t *parcel = (const uint8_t *)btd->data.ptr.buffer;
            size_t len = (size_t)btd->data_size;
            if (is_notification(parcel, len)) {
                log_notification(btd, parcel, len);
            }
        }
        off += payload;
    }
}
