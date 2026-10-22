# binder-sniffer build
#
# Three outputs:
#   make test     -> build/host_decode   host binary, runs the decoder offline
#   make sniffer  -> build/sniffer        arm64 binary (NDK), ptrace capture
#   make deploy   -> adb push the sniffer to the device
#
# The decoder sources are shared by both the host test and the sniffer.

BUILD      := build
DECODER_SRC := $(wildcard decoder/src/*.c)
INCLUDES    := -Idecoder/include
CFLAGS      := -Wall -Wextra -O2 -std=c11 $(INCLUDES)

# ---- Host decoder test (your Mac) -----------------------------------------
HOST_CC   ?= cc
HOST_BIN  := $(BUILD)/host_decode

# ---- Android sniffer (emulator) -------------------------------------------
# Override NDK / API / ABI as needed for your install.
NDK       ?= $(HOME)/Library/Android/sdk/ndk/29.0.13599879
API       ?= 33
TC        := $(NDK)/toolchains/llvm/prebuilt/darwin-x86_64
NDK_CC    := $(TC)/bin/aarch64-linux-android$(API)-clang
SNIFF_BIN := $(BUILD)/sniffer

# adb (SDK platform-tools)
ADB       ?= $(HOME)/Library/Android/sdk/platform-tools/adb
DEVDIR    ?= /data/local/tmp

.PHONY: all test sniffer deploy clean

all: test sniffer

test: $(HOST_BIN)
	@echo "run: $(HOST_BIN) tests/fixtures/<capture>.bin"

$(HOST_BIN): $(DECODER_SRC) tests/host_decode.c | $(BUILD)
	$(HOST_CC) $(CFLAGS) $^ -o $@

sniffer: $(SNIFF_BIN)

$(SNIFF_BIN): $(DECODER_SRC) sniffer/sniffer.c | $(BUILD)
	$(NDK_CC) $(CFLAGS) $^ -o $@

deploy: $(SNIFF_BIN)
	$(ADB) push $(SNIFF_BIN) $(DEVDIR)/sniffer
	$(ADB) shell chmod 755 $(DEVDIR)/sniffer

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
