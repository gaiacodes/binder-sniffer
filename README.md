# binder-sniffer

An Android binder IPC interceptor and Parcel decoder, for learning how binder
transactions are framed on the wire and how a schemaless Parcel can be decoded
back into meaningful fields.

Runs on a **rooted arm64 emulator** (API 33 / Android 13). This is an authorized
security-research / educational project on a personal developer emulator — not a
tool for targeting other people's apps.

## Status

| Component | State |
|-----------|-------|
| Pure-C Parcel decoder (`decoder/`)        | **Working**, unit-tested on the host against a golden fixture |
| In-app hook library (`hook/`)             | **Working**, GOT-patches libbinder's `ioctl` and decodes `INotificationManager` traffic live to logcat |
| ptrace injector (`injector/`)             | **Working**, loads the hook into a running app (attach → dlopen → detach) |

The full path is verified end-to-end on a rooted arm64 emulator (API 33):
`make deploy` → inject into the test app → a live `enqueueNotificationWithTag`
is intercepted and its `android.title` / `android.text` recovered from the Parcel.

## Architecture

A **standalone decoder** (pure C, no syscalls, no Android deps) with a **capture
front-end** wrapped around it. The decoder never knows where its bytes came from,
so it can be developed and unit-tested entirely on the host against a saved buffer.

Capture is done by **loading a hook library into a target app** and patching
`libbinder`'s `ioctl` call from inside the process:

```
   injector ──ptrace──▶ running app ──dlopen──▶ libbshook.so
   (delivery van;                                   │
    detaches after)                                 │ constructor GOT-patches
                                                      ▼ libbinder.so's ioctl import
        ┌─────────────────────────────┐       every binder ioctl now passes
        │   decoder/  (pure C core)    │◀───── through our hook, in-process
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

## Showcase

Full decode target: `android.app.INotificationManager` →
`enqueueNotificationWithTag` (transaction `code=7` on API 33), decoded down into the
`Notification`'s `extras` Bundle to recover `android.title` / `android.text`. The
hook sees *every* binder transaction the process makes but currently surfaces only
this one; all other transactions pass through untouched (extending the filter in
`hook/capture.c` is how you'd decode more — see [Using it on any app](#using-it-on-any-app)).

## Layout

| Path | What it is |
|------|------------|
| `decoder/include/bs_decoder.h`   | Public decoder API |
| `decoder/src/parcel.c`           | Parcel primitive reader (ints, UTF-16 strings, alignment, bounds) |
| `decoder/src/token.c`            | Parcel header skip + interface token read |
| `decoder/src/bundle.c`           | `Bundle` keyed value lookup (String / CharSequence) |
| `decoder/src/svc_notification.c` | `enqueueNotificationWithTag` decode → pkg / title / text |
| `hook/bshook.c`                  | Hook lib constructor: GOT-patch libbinder's `ioctl` |
| `hook/binder_cmd.c`              | BC_* command-stream walker (binder framing only) |
| `hook/capture.c`                 | Filter to `INotificationManager`, decode, log to logcat |
| `injector/injector.c`            | ptrace injector: attach, force `dlopen` of the hook, detach |
| `tests/host_decode.c`            | Host harness (reads a fixture, prints the decode) |
| `tests/run.sh`                   | Golden-output test runner used by `make test` and CI |
| `tests/fixtures/`, `tests/expected/` | Raw transaction captures (`*.bin`) and their golden decodes |
| `testapp/`                       | Gradle app that posts a notification to generate real traffic |
| `docs/`                          | Writeups: binder theory, protocol, AOSP source, project flow |

## Build & run

```sh
# Offline decoder test (runs on your Mac/Linux, no emulator).
# Decodes every fixture and diffs it against its golden output.
make test

# Build everything (host test + arm64 hook + arm64 injector; needs the NDK).
make all
```

Override `NDK` / `API` in the `Makefile` for your install. CI runs `make test`
on every push (see `.github/workflows/ci.yml`). To put the hook + injector on a
device, `make deploy` (below) builds and pushes them in one step — no separate
build needed first.

## Loading the hook

The injector loads `libbshook.so` into a running process using ptrace purely as a
**delivery van**: attach to the target PID, force it to call
`dlopen("/data/local/tmp/libbshook.so")` via a hijacked register/stack frame, then
detach. Once the hook's constructor runs and patches the GOT, the injector is gone
and the hook runs natively with no tracer attached.

The walkthrough below drives this with a single `make inject` (build → push →
inject), using the bundled test app as the target.

## Running the live demo from scratch

End-to-end on a rooted arm64 emulator. Steps 0–2 are one-time setup; 3–5 are the
actual demo. `$SDK` is your Android SDK path (`~/Library/Android/sdk` by default).

```sh
# 0. prerequisites: an arm64 system image + an AVD from it, and the NDK.
#    (This repo was verified against API 33 google_apis arm64-v8a.)
$SDK/emulator/emulator -list-avds          # confirm you have an AVD

# 1. boot the emulator and put it in lab state (rooted + permissive SELinux)
$SDK/emulator/emulator -avd <your-avd> -gpu host &
adb wait-for-device
adb root && adb shell setenforce 0

# 2. install the traffic-generating test app
(cd testapp && ./gradlew :app:installDebug)
#   or: adb install testapp/app/build/outputs/apk/debug/app-debug.apk

# 3. launch the app so it exists as a process, then start watching logcat
adb shell am start -n com.bindersniffer.testapp/.MainActivity
adb logcat -c && adb logcat -s binder-sniffer &

# 4. build + push + inject the hook into the running app
make inject

# 5. IMPORTANT: re-trigger a notification *after* the hook is installed.
#    The app posts in onCreate, which already ran at launch (step 3) before the
#    hook existed — so you must cause a fresh onCreate in the same process, or
#    logcat will show the GOT-patch lines but no decode. Starting the activity
#    with NEW_TASK|MULTIPLE_TASK creates a new instance without killing the
#    process (a restart would spawn a new PID and lose the hook).
adb shell am start -n com.bindersniffer.testapp/.MainActivity -f 0x18000000
```

Expected logcat once step 5 fires the notification through the hooked `ioctl`:

```
binder-sniffer: loaded
binder-sniffer: found libbinder
binder-sniffer: ioctl got_slot=0x...  *got_slot=0x...
binder-sniffer: got_slot page now writable
binder-sniffer: pkg=com.bindersniffer.testapp title="Binder Sniffer" text="hello from testapp"
```

`make inject` targets `com.bindersniffer.testapp` by default; override `PKG` (and
`DEVDIR`, the on-device path) on the `make` command line to hook a different app.

> **On the load path:** this flow was verified end-to-end loading the `.so` from
> `/data/local/tmp` on the permissive lab emulator (rooted, `setenforce 0`, API 33
> `google_apis` image), where the app's linker namespace permits it and `dlopen`
> returns a live handle. On a device with a stricter app namespace that `dlopen`
> can return 0 (fail); there the `.so` must live somewhere the target permits —
> see [Environment notes](#environment-notes) for the `/system/lib64` fallback.

## Using it on any app

`testapp/` is only a convenient, self-contained target — nothing in the hook or
injector is specific to it. The intercept is generic: it patches `ioctl` in the
target process's `libbinder`, so it sees **every** binder transaction that process
makes, regardless of which app it is.

To point it at a real app instead of the test app:

```sh
# the app must already be running (so it has a PID); PKG is any installed package
make inject PKG=com.example.someapp
adb logcat -s binder-sniffer
```

Then **use the app normally** to make it do the thing you want to observe — for
the current decoder, post a notification (send yourself a message, trigger an
alert, etc.). You do *not* need the `am start -f 0x18000000` re-trigger from the
demo: that step only exists because the test app posts once in `onCreate` at
launch. A real app posts notifications as you interact with it, and the hook —
already installed — catches each one live.

What you'll see, and what you won't:

- **Captured today:** that app's `INotificationManager.enqueueNotificationWithTag`
  calls, decoded to `pkg` / `title` / `text`. Works for any app that posts
  notifications.
- **Not yet surfaced:** every other binder call the app makes. The hook receives
  them but `hook/capture.c` filters to `INotificationManager` and ignores the
  rest. To decode a different interface/method, widen the filter in `capture.c`
  and add a decoder for that method's Parcel layout (the `decoder/` core is where
  that logic goes — `svc_notification.c` is the worked example to copy).
- **Won't work:** hardened apps that resist ptrace injection or the GOT patch
  (see [Known limitations](#known-limitations)).

## Environment notes

- Emulator must run with `-gpu host` on Apple Silicon.
- `adb root` + `adb shell setenforce 0` (permissive) for the lab.
- NDK cross-compiler: `aarch64-linux-android33-clang` (override `NDK` / `API` in the
  Makefile for your path).
- The hook `.so` loads from `/data/local/tmp` on this emulator; a stricter app
  namespace may require `/system/lib64` (`adb disable-verity` + reboot + remount).
- The test app's Gradle JDK is set in `~/.gradle/gradle.properties`, not committed,
  so the repo stays portable.

## Known limitations

- Parcels are schemaless — the wire format carries no type info. Full argument
  decode for any given method needs that method's AIDL signature. This project
  decodes the one showcase method fully and header-decodes everything else.
- Works on ordinary apps. Hardened apps (anti-debug: self-ptrace, `TracerPid`
  checks, `PR_SET_DUMPABLE(0)`, GOT integrity scans) resist both the injection and
  the hook; a kernel kprobe on `binder_transaction` is the uncircumventable path.
