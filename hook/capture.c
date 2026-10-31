#include "capture.h"
#include "binder_cmd.h"  // bs_walk_bc_stream
#include "bs_decoder.h"  // bs_decode_enqueue, bs_notif

#include <stdint.h>      // uint8_t
#include <string.h>      // strlen
#include <android/log.h> // __android_log_print

// The interface we care about. Its descriptor sits near the start of the
// transaction's Parcel, stored as UTF-16LE.
#define WANT_IFACE "android.app.INotificationManager"

// Does this transaction's Parcel target INotificationManager? The descriptor is
// right after the small header, so scanning the first chunk is enough. The
// UTF-16LE scan itself lives in the decoder (bs_find_utf16).
static int is_notification(const uint8_t *parcel, size_t len) {
    if (!parcel) return 0;
    size_t scan = len < 256 ? len : 256;
    return bs_find_utf16(parcel, scan, WANT_IFACE, 0) >= 0;
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

// Per-transaction callback for bs_walk_bc_stream: follow the payload pointer to
// the Parcel (in our own address space, valid for this ioctl call) and, if it
// targets INotificationManager, decode and log it.
static void on_transaction(const struct binder_transaction_data *btd, void *user) {
    (void)user;
    const uint8_t *parcel = (const uint8_t *)btd->data.ptr.buffer;
    size_t len = (size_t)btd->data_size;
    if (is_notification(parcel, len)) {
        log_notification(btd, parcel, len);
    }
}

void bs_capture_bwr(const struct binder_write_read *bwr) {
    // Transport walking lives in binder_cmd.c; here we only care about what each
    // transaction means. Decode and log every INotificationManager transaction.
    bs_walk_bc_stream(bwr, on_transaction, NULL);
}
