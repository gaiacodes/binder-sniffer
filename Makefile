# binder-sniffer build
#
# Architecture: a pure-C decoder core (decoder/) linked into two things:
#   - a host test harness  (runs on your Mac, decodes a saved fixture)
#   - the hook library     (runs inside the target app, feeds the decoder live)
# plus a standalone injector that loads the hook into a running app.
#
#   make test      -> build/host_decode    host binary, decode a fixture offline
#   make hook      -> build/libbshook.so   arm64, injected into the app
#   make injector  -> build/injector       arm64, runtime .so injector
#   make deploy    -> adb push hook + injector to the device
#   make clean

BUILD       := build
DECODER_SRC := $(wildcard decoder/src/*.c)
HOOK_SRC    := $(wildcard hook/*.c)
INCLUDES    := -Idecoder/include
CFLAGS      := -Wall -Wextra -O2 -std=c11 $(INCLUDES)

# ---- Host decoder test (your Mac) -----------------------------------------
HOST_CC   ?= cc
HOST_BIN  := $(BUILD)/host_decode

# ---- Android targets (emulator), NDK cross-build --------------------------
# Override NDK / API as needed for your install.
NDK       ?= $(HOME)/Library/Android/sdk/ndk/29.0.13599879
API       ?= 33
TC        := $(NDK)/toolchains/llvm/prebuilt/darwin-x86_64
NDK_CC    := $(TC)/bin/aarch64-linux-android$(API)-clang

HOOK_BIN  := $(BUILD)/libbshook.so
INJ_BIN   := $(BUILD)/injector

# adb (SDK platform-tools)
ADB       ?= $(HOME)/Library/Android/sdk/platform-tools/adb
DEVDIR    ?= /data/local/tmp

.PHONY: all test hook injector deploy clean

all: test hook injector

# --- host offline decode ---
test: $(HOST_BIN)
	@echo "run: $(HOST_BIN) tests/fixtures/<capture>.bin"

$(HOST_BIN): $(DECODER_SRC) tests/host_decode.c | $(BUILD)
	$(HOST_CC) $(CFLAGS) $^ -o $@

# --- in-app hook library (decoder linked in) ---
hook: $(HOOK_BIN)

$(HOOK_BIN): $(DECODER_SRC) $(HOOK_SRC) | $(BUILD)
	$(NDK_CC) $(CFLAGS) -shared -fPIC $^ -o $@ -llog

# --- runtime injector ---
injector: $(INJ_BIN)

$(INJ_BIN): injector/injector.c | $(BUILD)
	$(NDK_CC) $(CFLAGS) $^ -o $@

# --- push both to the device ---
deploy: $(HOOK_BIN) $(INJ_BIN)
	$(ADB) push $(HOOK_BIN) $(DEVDIR)/libbshook.so
	$(ADB) push $(INJ_BIN)  $(DEVDIR)/injector
	$(ADB) shell chmod 755 $(DEVDIR)/injector $(DEVDIR)/libbshook.so

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
