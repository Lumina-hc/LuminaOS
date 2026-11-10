#!/usr/bin/env bash
set -euo pipefail

HEADLESS=0
TRACE=0
for arg in "$@"; do
    case "$arg" in
        --headless) HEADLESS=1 ;;
        --trace)    TRACE=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
img="$ROOT/build64/floppy.img"
hdd="$ROOT/build64/hdd.img"

if [[ ! -f "$img" ]]; then
    echo -e "\033[31mError: $img not found. Run build.sh first.\033[0m"
    exit 1
fi

QEMU_ARGS=(
    -drive "file=$img,format=raw,if=floppy"
    -boot a
    -m 32M
)

if [[ -f "$hdd" ]]; then
    QEMU_ARGS+=(-drive "file=$hdd,format=raw,if=ide,media=disk")
fi

if [[ $HEADLESS -eq 1 ]]; then
    QEMU_ARGS+=(-display none)
else
    QEMU_ARGS+=(-display sdl)
fi

QEMU_ARGS+=(-debugcon "file:$ROOT/build64/debug.log")

if [[ $TRACE -eq 1 ]]; then
    QEMU_ARGS+=(-d cpu_reset,int -D "$ROOT/build64/qemu-trace.log")
fi

echo -e "\033[36mStarting LuminaOS x86_64 in QEMU...\033[0m"
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"