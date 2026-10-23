#include "bs_decoder.h"

// writeInterfaceToken (Android 13) writes, in order:
//   int32 strict-mode policy
//   int32 work-source uid  (-1 when unset)
//   int32 RPC header = 'SYST'  (B_PACK_CHARS('S','Y','S','T') = 0x53595354)
//   String16 interface descriptor
// We confirmed this layout against the captured fixture.
#define BS_RPC_HEADER 0x53595354

int bs_read_interface_token(bs_parcel *p, char *out, size_t outcap) {
    (void)bs_read_i32(p);              // strict-mode policy
    (void)bs_read_i32(p);              // work-source uid
    (void)bs_read_i32(p);              // RPC header ('SYST'); not validated
    if (p->error) return 1;
    return bs_read_string16(p, out, outcap);
}
