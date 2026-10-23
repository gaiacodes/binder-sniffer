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

    // 3. title/text from the extras Bundle (keyed lookup over the payload)
    bs_parcel whole;
    bs_parcel_init(&whole, buf, len);
    out->have_title = (bs_bundle_get_charseq(&whole, "android.title",
                       out->title, sizeof(out->title)) == 0);
    out->have_text  = (bs_bundle_get_charseq(&whole, "android.text",
                       out->text, sizeof(out->text)) == 0);

    return 0;
}
