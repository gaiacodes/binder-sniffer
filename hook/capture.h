#pragma once
#include <linux/android/binder.h> // struct binder_write_read

// Live interception entry point. Given a BINDER_WRITE_READ's binder_write_read
// (as seen by the hooked ioctl), walk its command stream, and for every
// transaction targeting INotificationManager, decode the Parcel payload and log
// the recovered pkg/title/text to logcat. Runs in-process on the binder thread;
// the decoder it calls is read-only and stateless, so this is safe there.
void bs_capture_bwr(const struct binder_write_read *bwr);
