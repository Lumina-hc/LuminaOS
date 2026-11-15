#!/usr/bin/env bash
# Build the SDL3 LuminaOS demo as a freestanding ELF64 user app.
# Produces demo.elf, ready to be copied onto the hdd image by build.ps1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SDL="$ROOT/third_party/SDL"
SDK="$ROOT/third_party/LuminaOS-SDK"
COMPAT="$ROOT/ports/sdl3/compat"
LIB="$ROOT/ports/sdl3/lib/libSDL3.a"
OUT="$(dirname "$0")/demo.elf"

CFLAGS=(-ffreestanding -nostdlib -fno-pic -fno-pie -m64
        -mno-red-zone -mno-mmx -mno-80387 -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -ffunction-sections -fdata-sections
        -O2 -Wall -std=gnu11
        -D__LUMIA__=1 -DSDL_MAIN_HANDLED
        -DSDL_SLOW_MEMCPY -DSDL_SLOW_MEMMOVE -DSDL_SLOW_MEMSET
        -I"$COMPAT/include" -I"$SDL/include" -I"$SDL/include/build_config")

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "=== Compiling demo + compat libc ==="
x86_64-elf-gcc "${CFLAGS[@]}" -c "$(dirname "$0")/demo.c" -o "$WORK/demo.o"
x86_64-elf-gcc "${CFLAGS[@]}" -c "$COMPAT/libc.c"          -o "$WORK/libc.o"

echo "=== Linking demo.elf ==="
# libc.o is listed once (first) so the archive resolves SDL's libc-y
# references; a second copy would cause multiple-definition errors.
# --gc-sections drops the unused SDL code; -e _start keeps the entry point.
x86_64-elf-ld -T "$SDK/elf.ld" --gc-sections -e _start -o "$OUT" \
    "$WORK/demo.o" "$WORK/libc.o" "$LIB"

echo "=== Result ==="
ls -lh "$OUT"
x86_64-elf-objdump -h "$OUT" | sed -n '1,20p'
