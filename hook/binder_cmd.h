#pragma once
#include <linux/android/binder.h> // struct binder_write_read, binder_transaction_data

// Transport-level walk of a binder BC_* command stream. This layer knows only
// about binder framing, not about any particular interface or Parcel contents.

// Invoked once per BC_TRANSACTION / BC_TRANSACTION_SG command in the stream.
// `btd` points into the live write buffer and is valid only for the duration of
// the call. `user` is passed through unchanged from bs_walk_bc_stream.
typedef void (*bs_txn_fn)(const struct binder_transaction_data *btd, void *user);

// Walk the command stream in `bwr`'s write buffer, calling `cb` for each
// transaction command. Each command code carries its own payload size in the
// ioctl _IOC_SIZE field, so the stream can be stepped without a size table.
void bs_walk_bc_stream(const struct binder_write_read *bwr,
                       bs_txn_fn cb, void *user);
