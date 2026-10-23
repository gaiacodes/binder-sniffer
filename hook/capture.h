#pragma once
#include <linux/android/binder.h> // struct binder_write_read

// Throwaway sample-gathering tool: given a BINDER_WRITE_READ's binder_write_read,
// walk its command stream and dump each transaction's Parcel payload to a file in
// the app's data dir. Used only to capture fixtures for the offline decoder — it
// is NOT part of the real interception path and will be removed once we have the
// fixtures we need.
void bs_capture_bwr(const struct binder_write_read *bwr);
