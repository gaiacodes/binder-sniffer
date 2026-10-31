#include "bs_decoder.h"
#include <string.h>

// enqueueNotificationWithTag(String pkg, String opPkg, String tag, int id,
//                            in Notification notification, int userId)
//
// Pragmatic decode: parse the interface token and the leading args properly,
// then recover the title/text from the Notification's extras Bundle by keyed
// lookup rather than walking every Notification field (the version-fragile
// part). The Bundle values themselves are decoded structurally.
int bs_decode_enqueue(const uint8_t *buf, size_t len, bs_notif *out) {
    memset(out, 0, sizeof(*out));

    bs_parcel p;
    bs_parcel_init(&p, buf, len);

    // 1. interface token
    if (bs_read_interface_token(&p, out->interface, sizeof(out->interface)))
        return 1;

    // 2. leading AIDL args (proper, in order)
    bs_read_string16(&p, out->pkg, sizeof(out->pkg));  // pkg
    bs_skip_string16(&p);                               // opPkg
    bs_skip_string16(&p);                               // tag (nullable)
    (void)bs_read_i32(&p);                              // id
    if (p.error) return 1;
    // The Notification parcelable + userId follow; we don't walk them here.

    // 3. title/text from the extras Bundle. Both keys are found in a single
    // pass over the payload (this runs on the binder hot path), then each
    // value is decoded from its recorded offset.
    bs_parcel whole;
    bs_parcel_init(&whole, buf, len);

    const char *keys[2] = { "android.title", "android.text" };
    long off[2] = { -1, -1 };
    size_t remaining = 2;
    for (size_t i = 0; i < len && remaining; i++) {
        for (int k = 0; k < 2; k++) {
            if (off[k] < 0 && bs_match_utf16_at(buf, len, i, keys[k], 1)) {
                off[k] = (long)i;
                remaining--;
            }
        }
    }
    if (off[0] >= 0)
        out->have_title = (bs_bundle_value_at(&whole, (size_t)off[0], strlen(keys[0]),
                           out->title, sizeof(out->title)) == 0);
    if (off[1] >= 0)
        out->have_text  = (bs_bundle_value_at(&whole, (size_t)off[1], strlen(keys[1]),
                           out->text, sizeof(out->text)) == 0);

    return 0;
}
