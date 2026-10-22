# binder-sniffer

An Android binder IPC interceptor and Parcel decoder, for learning how
binder transactions are framed on the wire and how a schemaless Parcel
can be decoded back into meaningful fields.

Runs on a **rooted arm64 emulator** (API 33 / Android 13). This is an
authorized security-research / educational project on a personal
developer emulator — not a tool for targeting other people's apps.

## Architecture

A **standalone decoder** (pure C, no syscalls, no Android deps) with a
**capture front-end** wrapped around it. The decoder never knows where
its bytes came from, so it can be developed and unit-tested entirely on
the host against a saved buffer.

```
        ┌─────────────────────────────┐
        │   decoder/  (pure C core)    │
        │  raw txn bytes → decoded text│
        └──────────────┬──────────────┘
                       │ linked into
          ┌────────────┴────────────┐
          ▼                         ▼
   tests/host_decode.c        sniffer/sniffer.c
   (offline, on host)         (ptrace, on device)
```

> An earlier design also had a Stage A `LD_PRELOAD` front-end. It works
> only for native binder clients you launch yourself — Android's linker
> namespace isolation stops it from hooking `ioctl` inside zygote-spawned
> GUI apps — so for real apps ptrace is the only path, and Stage A was
> dropped. The reference implementation lives in `../binder-poc/`.

## Showcase

Full decode target: `android.app.INotificationManager` →
`enqueueNotificationWithTag` (transaction `code=7` on API 33), decoded
down into the `Notification`'s `extras` Bundle to recover
`android.title` / `android.text`. Every other transaction is
header-decoded (interface token, code, sizes) and hex-dumped.

## Layout

| Path | What it is |
|------|------------|
| `decoder/include/bs_decoder.h` | Public decoder API |
| `decoder/src/parcel.c`         | Parcel primitive reader (ints, UTF-16 strings, alignment, bounds) |
| `decoder/src/token.c`          | Parcel header skip + interface token read |
| `decoder/src/txn.c`            | `binder_transaction_data` parse (verified 64-bit offsets) |
| `decoder/src/binder_cmd.c`     | BC_* command-stream walker |
| `decoder/src/bundle.c`         | Ordered `Bundle` key/value parse |
| `decoder/src/svc_notification.c` | `Notification` parcelable walk → title/text |
| `sniffer/sniffer.c`            | ptrace capture: attach to pid, read binder buffers, feed the decoder |
| `tests/host_decode.c`          | Host test harness (reads a fixture, prints the decode) |
| `tests/fixtures/`              | Golden raw transaction captures (`*.bin`) |
| `testapp/`                     | Gradle app that posts a notification to generate real traffic |

## Build & run

```sh
# Offline decoder test (runs on your Mac, no emulator)
make test

# Cross-compile the sniffer for the emulator (needs NDK)
make sniffer

# Push it to the device
make deploy
```

Then on the device, as root:

```sh
./sniffer $(pidof com.bindersniffer.testapp)
# trigger a notification in the app; watch the decode
```

## Environment notes

- Emulator must run with `-gpu host` on Apple Silicon.
- `adb root` + `adb shell setenforce 0` (permissive) for the lab.
- NDK cross-compiler: `aarch64-linux-android33-clang` (override `NDK` /
  `CC` in the Makefile for your path).
- The test app's Gradle JDK (21) is set in `~/.gradle/gradle.properties`,
  not committed, so the repo stays portable.

## Known limitation

Parcels are schemaless — the wire format carries no type info. Full
argument decode for any given method needs that method's AIDL signature.
This project decodes the one showcase method fully and header-decodes
everything else.
