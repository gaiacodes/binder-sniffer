#pragma once
#include <stddef.h>
#include <stdint.h>

// --- Parcel cursor ---------------------------------------------------------
// A read cursor over a Parcel's bytes. `error` latches on the first malformed
// or out-of-bounds read so callers can check once at the end.
typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
    int            error;
} bs_parcel;

// --- Decode result ---------------------------------------------------------
// What we recover from an enqueueNotificationWithTag parcel.
typedef struct {
    char interface[128];   // interface descriptor (e.g. android.app.INotificationManager)
    char pkg[128];         // first AIDL arg: calling package
    char title[256];       // extras "android.title"
    char text[256];        // extras "android.text"
    int  have_title;
    int  have_text;
} bs_notif;

// --- parcel.c : primitive readers ------------------------------------------
void    bs_parcel_init(bs_parcel *p, const uint8_t *data, size_t size);
int32_t bs_read_i32(bs_parcel *p);
int64_t bs_read_i64(bs_parcel *p);
void    bs_align4(bs_parcel *p);
// Reads a length-prefixed UTF-16 String16 into `out` (ASCII subset; non-ASCII
// code units become '?'). A null string (len -1) yields "". 0 on success.
int     bs_read_string16(bs_parcel *p, char *out, size_t outcap);
void    bs_skip_string16(bs_parcel *p);

// --- token.c : binder interface token --------------------------------------
// Consumes the transaction header (strict-mode, work-source, 'SYST') and reads
// the interface descriptor String16 into `out`. 0 on success.
int bs_read_interface_token(bs_parcel *p, char *out, size_t outcap);

// --- bundle.c : extras lookup ----------------------------------------------
// Finds `key` (an ASCII Bundle key) in the whole parcel and decodes the
// String/CharSequence value that follows it into `out`. 0 on success.
int bs_bundle_get_charseq(const bs_parcel *whole, const char *key,
                          char *out, size_t outcap);

// --- svc_notification.c : the showcase decode ------------------------------
// Decodes a captured enqueueNotificationWithTag Parcel payload. 0 on success.
int bs_decode_enqueue(const uint8_t *buf, size_t len, bs_notif *out);
