# binder-sniffer

An Android binder IPC interceptor and Parcel decoder, for learning how binder
transactions are framed on the wire and how a schemaless Parcel can be decoded
back into meaningful fields.

Runs on a **rooted arm64 emulator** (API 33 / Android 13). This is an authorized
security-research / educational project on a personal developer emulator — not a
tool for targeting other people's apps.

## Architecture

A **standalone decoder** (pure C, no syscalls, no Android deps) with a **capture
front-end** wrapped around it. The decoder never knows where its bytes came from,
so it can be developed and unit-tested entirely on the host against a saved buffer.

Capture is done by **injecting a hook library into a running app at runtime** and
patching `libbinder`'s `ioctl` call:

```
   injector ──ptrace──▶ running app ──dlopen──▶ libbshook.so
   (delivery van;                                   │
    detaches after)                                 │ constructor GOT-patches
                                                     ▼ libbinder.so's ioctl import
        ┌─────────────────────────────┐      every binder ioctl now passes
        │   decoder/  (pure C core)    │◀──── through our hook, in-process
        │  raw txn bytes → decoded text│
        └──────────────┬──────────────┘
                       │ also linked into
                       ▼
                tests/host_decode.c   (offline, on host)
```

Why this capture method:
- **ptrace is only the delivery van** — it attaches just long enough to force the
  app to `dlopen` the hook, then detaches. The hook then runs natively with no
  tracer attached and no slowdown.
- **GOT patching, not LD_PRELOAD interposition** — we overwrite libbinder's
  already-resolved `ioctl` pointer from *inside* the process, so Android's linker
  namespace isolation (which defeats LD_PRELOAD) is irrelevant.
- **One patch is process-wide** — every thread (main + binder threads) is captured.
- **Reads are plain dereferences** — in-process, so no `process_vm_readv`.

> Feasibility of this whole path is proven end-to-end in `../binder-poc/inject-hook/`
> (see its `FINDINGS.md`): a live notification's `android.title`/`android.text` were
> recovered from a real running app. This repo is the clean, real implementation.

## Showcase

Full decode target: `android.app.INotificationManager` →
`enqueueNotificationWithTag` (transaction `code=7` on API 33), decoded down into the
`Notification`'s `extras` Bundle to recover `android.title` / `android.text`. Every
other transaction is header-decoded (interface token, code, sizes) and hex-dumped.

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
| `hook/bshook.c`                | Hook lib: GOT-patch libbinder `ioctl`, feed the decoder |
| `injector/injector.c`          | Runtime ptrace injector: attach, dlopen the hook, detach |
| `tests/host_decode.c`          | Host test harness (reads a fixture, prints the decode) |
| `tests/fixtures/`              | Golden raw transaction captures (`*.bin`) |
| `testapp/`                     | Gradle app that posts a notification to generate real traffic |

## Build & run

```sh
# Offline decoder test (runs on your Mac, no emulator)
make test

# Cross-compile the hook + injector for the emulator (needs NDK)
make hook injector

# Push them to the device
make deploy
```

Then on the device, as root, against a running app:

```sh
PID=$(adb shell pidof com.bindersniffer.testapp)
adb shell su 0 /data/local/tmp/injector $PID /data/local/tmp/libbshook.so
adb logcat -s BSHOOK:I            # watch the decode
```

## Environment notes

- Emulator must run with `-gpu host` on Apple Silicon.
- `adb root` + `adb shell setenforce 0` (permissive) for the lab.
- NDK cross-compiler: `aarch64-linux-android33-clang` (override `NDK` / `API` in the
  Makefile for your path).
- The hook `.so` loaded from `/data/local/tmp` on this emulator; a stricter app
  namespace may require `/system/lib64` (`adb disable-verity` + reboot + remount).
- The test app's Gradle JDK (21) is set in `~/.gradle/gradle.properties`, not
  committed, so the repo stays portable.

## Known limitations

- Parcels are schemaless — the wire format carries no type info. Full argument
  decode for any given method needs that method's AIDL signature. This project
  decodes the one showcase method fully and header-decodes everything else.
- Works on ordinary apps. Hardened apps (anti-debug: self-ptrace, `TracerPid`
  checks, `PR_SET_DUMPABLE(0)`, GOT integrity scans) resist both the injection and
  the hook; a kernel kprobe on `binder_transaction` is the uncircumventable path.
