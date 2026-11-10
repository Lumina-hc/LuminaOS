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
BUILD="$ROOT/build64"
ESP="$BUILD/uefi.img"
HDD="$BUILD/hdd.img"
CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
VARS="/usr/share/OVMF/OVMF_VARS_4M.fd"


if [[ ! -f "$ESP" ]]; then
    echo -e "\033[31mError: $ESP not found. Run build_uefi.sh first.\033[0m"
    exit 1
fi

# ---------------------------------------------------------------------------
# 计算 vars 区大小（1MiB 对齐，与 OVMF code 映像对齐）
# ---------------------------------------------------------------------------
IFS=$'\t' read -r code_len vars_size <<< "$(python3 - "$CODE" <<'PYEOF'
import sys, math
code_len = len(open(sys.argv[1], "rb").read())
need = 1024 * 1024 * (1 + (code_len - 1) // (1024 * 1024))
print(code_len, need - code_len)
PYEOF
)"

VARS_COPY="$BUILD/ovmf_vars.fd"
if [[ ! -f "$VARS_COPY" ]]; then
    if [[ -f "$VARS" ]]; then
        cp "$VARS" "$VARS_COPY"
    else
        # Fallback：与 code 映像 var 区同尺寸的全 0xFF NVRAM
        python3 - "$VARS_COPY" "$vars_size" <<'PYEOF'
import sys
path, size = sys.argv[1], int(sys.argv[2])
with open(path, "wb") as f:
    f.write(b"\xFF" * size)
PYEOF
    fi
fi

# ---------------------------------------------------------------------------
# 结束已有 qemu-system 进程（仅当 pkill 可用；Windows 下通常无）
# ---------------------------------------------------------------------------
if command -v pkill >/dev/null 2>&1; then
    pkill -f "qemu-system" 2>/dev/null || true
elif command -v taskkill.exe >/dev/null 2>&1; then
    taskkill.exe //F //IM qemu-system-x86_64.exe 2>/dev/null || true
fi

QEMU_ARGS=(
    -drive "if=pflash,format=raw,readonly=on,file=$CODE"
    -drive "if=pflash,format=raw,file=$VARS_COPY"
    -m 128M
    -rtc base=localtime
    -vga std
)

# hdd.img 必须保持 primary master（内核 ATA 读 0x1F0）
if [[ -f "$HDD" ]]; then
    QEMU_ARGS+=(-drive "file=$HDD,format=raw,if=ide,media=disk")
fi
QEMU_ARGS+=(-drive "file=$ESP,format=raw,if=ide,media=disk")

if [[ $HEADLESS -eq 1 ]]; then
    QEMU_ARGS+=(-display none -monitor "tcp:127.0.0.1:5555,server,nowait")
else
    QEMU_ARGS+=(-display "gtk,zoom-to-fit=on,full-screen=on")
fi

QEMU_ARGS+=(-debugcon "file:$BUILD/uefi-debug.log")
QEMU_ARGS+=(-chardev "file,id=ovmf,path=$BUILD/ovmf-debug.log")
QEMU_ARGS+=(-device "isa-debugcon,iobase=0x402,chardev=ovmf")

if [[ $TRACE -eq 1 ]]; then
    QEMU_ARGS+=(-d cpu_reset,int,in_asm -D "$BUILD/qemu-uefi-trace.log")
fi

echo -e "\033[36mStarting LuminaOS x86_64 with OVMF (UEFI)...\033[0m"
export SDL_VIDEO_CENTERED=1
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
