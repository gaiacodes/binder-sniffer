#include "capture.h"

#include <fcntl.h>       // open, O_WRONLY/O_CREAT/O_TRUNC
#include <unistd.h>      // write, close
#include <stdint.h>      // uint32_t, uint8_t, uintptr_t
#include <stdio.h>       // snprintf
#include <string.h>      // strlen
#include <errno.h>       // errno
#include <android/log.h> // __android_log_print

// The app's own data dir — the app can write here (unlike /data/local/tmp,
// which belongs to shell and is denied to the app uid).
#define CAP_DIR "/data/data/com.bindersniffer.testapp"

// The interface we care about. Its descriptor sits near the start of the
// transaction's Parcel, stored as UTF-16LE.
#define WANT_IFACE "android.app.INotificationManager"

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

// Write one transaction's Parcel payload to a file. The payload bytes live
// behind data.ptr.buffer, not inline in the command stream, so we follow it.
static void dump_transaction(const struct binder_transaction_data *btd) {
    static int seq = 0;
    int n = __sync_fetch_and_add(&seq, 1);   // atomic: binder threads run concurrently

    char path[160];
    snprintf(path, sizeof(path), CAP_DIR "/notif_code%u_%d.bin", btd->code, n);

    int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "HOOK",
            "capture open failed errno=%d path=%s", errno, path);
        return;
    }
    ssize_t w = write(out, (const void *)btd->data.ptr.buffer,
                      (size_t)btd->data_size);
    close(out);
    __android_log_print(ANDROID_LOG_INFO, "HOOK",
        "captured notification code=%u wrote=%zd/%zu -> %s",
        btd->code, w, (size_t)btd->data_size, path);
}

void bs_capture_bwr(const struct binder_write_read *bwr) {
    // Walk the command stream. Each command is a u32 code whose payload size is
    // encoded in the code itself (_IOC_SIZE = bits 16..29), so we can step from
    // one command to the next without a per-command size table. For each
    // transaction, dump it only if it targets INotificationManager.
    uintptr_t base = (uintptr_t)bwr->write_buffer;
    size_t off = 0;
    while (off + 4 <= (size_t)bwr->write_size) {
        uint32_t cmd = *(uint32_t *)(base + off);
        uint32_t payload = (cmd >> 16) & 0x3FFF;   // _IOC_SIZE
        off += 4;
        if (cmd == BC_TRANSACTION || cmd == BC_TRANSACTION_SG) {
            const struct binder_transaction_data *btd =
                (const struct binder_transaction_data *)(base + off);
            if (is_notification((const uint8_t *)btd->data.ptr.buffer,
                                 (size_t)btd->data_size)) {
                dump_transaction(btd);
            }
        }
        off += payload;
    }
}
