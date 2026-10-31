#!/bin/sh
# Golden-output test for the offline decoder.
#
# For every tests/fixtures/*.bin that has a matching tests/expected/*.txt,
# run the host decoder and diff its output against the golden file. Exits
# non-zero on the first mismatch so `make test` and CI fail loudly.
#
#   usage: tests/run.sh <path-to-host_decode>

set -eu

BIN="${1:-build/host_decode}"
DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
FIXTURES="$DIR/fixtures"
EXPECTED="$DIR/expected"

if [ ! -x "$BIN" ]; then
    echo "test: decoder binary not found or not executable: $BIN" >&2
    exit 2
fi

ran=0
fail=0
for bin in "$FIXTURES"/*.bin; do
    [ -e "$bin" ] || continue
    name="$(basename "$bin" .bin)"
    golden="$EXPECTED/$name.txt"
    if [ ! -f "$golden" ]; then
        echo "SKIP $name (no golden file at expected/$name.txt)"
        continue
    fi
    ran=$((ran + 1))
    if got="$("$BIN" "$bin")" && printf '%s\n' "$got" | diff -u "$golden" - >/dev/null; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        printf '%s\n' "$got" | diff -u "$golden" - || true
        fail=$((fail + 1))
    fi
done

if [ "$ran" -eq 0 ]; then
    echo "test: no fixtures with golden output were found" >&2
    exit 2
fi
if [ "$fail" -ne 0 ]; then
    echo "test: $fail of $ran fixture(s) failed" >&2
    exit 1
fi
echo "test: all $ran fixture(s) passed"
