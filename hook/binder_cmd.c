#include "binder_cmd.h"
#include <stdint.h> // uint32_t, uintptr_t
#include <stddef.h> // size_t

// A binder command code carries its payload size in bits 16..29 (the ioctl
// _IOC_SIZE field), so we can step over each command without a size table.
#define BC_PAYLOAD_SIZE(cmd) (((cmd) >> 16) & 0x3FFF)

void bs_walk_bc_stream(const struct binder_write_read *bwr,
                       bs_txn_fn cb, void *user) {
    uintptr_t base = (uintptr_t)bwr->write_buffer;
    size_t off = 0;
    while (off + 4 <= (size_t)bwr->write_size) {
        uint32_t cmd = *(uint32_t *)(base + off);
        uint32_t payload = BC_PAYLOAD_SIZE(cmd);
        off += 4;
        if (cmd == BC_TRANSACTION || cmd == BC_TRANSACTION_SG) {
            const struct binder_transaction_data *btd =
                (const struct binder_transaction_data *)(base + off);
            cb(btd, user);
        }
        off += payload;
    }
}
