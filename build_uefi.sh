#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build64"
EFI_DIR="$ROOT/boot/efi"
TOOLCHAIN="/d/programs/x86_64-elf-tools-windows/bin"
MINGW_BIN="/d/programs/msys2/mingw64/bin"
LLVM_BIN="/d/programs/LLVM/bin"

export PATH="$TOOLCHAIN:$MINGW_BIN:$LLVM_BIN:$PATH"

run_tool() {
    local exe="$1"; shift
    "$exe" "$@" || { echo "ERROR: $exe failed (exit $?)" >&2; exit 1; }
}

get_symbol_addr() {
    local name="$1" addr
    addr=$(x86_64-elf-nm "$BUILD/kernel.elf" \
        | awk -v sym="$name" '$3 == sym { print $1; exit }')
    [[ -n "$addr" ]] || { echo "symbol $name not found" >&2; exit 1; }
    echo $((16#$addr))
}

echo -e "\033[36m=== LuminaOS UEFI Build ===\033[0m"

if [[ ! -f "$BUILD/kernel.elf" ]]; then
    echo "  Kernel not built, running build.sh first..."
    bash "$ROOT/build.sh"
fi

entry=$(get_symbol_addr "efi_entry")
bssStart=$(get_symbol_addr "bss_start")
bssEnd=$(get_symbol_addr "bss_end")
printf "  efi_entry=0x%X  bss=0x%X..0x%X\n" "$entry" "$bssStart" "$bssEnd"

mkdir -p "$BUILD"

echo "[1/3] Compiling EFI loader..."
defEntry=$(printf "0x%X" "$entry")
defBssStart=$(printf "0x%X" "$bssStart")
defBssEnd=$(printf "0x%X" "$bssEnd")

LOADER_FLAGS=(
    -ffreestanding -fno-builtin -fno-stack-protector
    -mno-red-zone -mno-stack-arg-probe -Wall -Wextra -O2
    "-I$EFI_DIR"
    "-DKERNEL_ENTRY=$defEntry"
    "-DKERNEL_BSS_START=$defBssStart"
    "-DKERNEL_BSS_END=$defBssEnd"
    -c "$EFI_DIR/efi_loader.c" -o "$BUILD/efi_loader.o"
)
run_tool x86_64-w64-mingw32-gcc "${LOADER_FLAGS[@]}"

echo "[2/3] Linking BOOTX64.EFI..."
run_tool lld-link \
    "/out:$BUILD/BOOTX64.EFI" \
    /entry:efi_main \
    /subsystem:efi_application \
    /nodefaultlib \
    /base:0x200000 \
    "$BUILD/efi_loader.o"

echo "[3/3] Creating uefi.img (GPT ESP)..."

# ---------------------------------------------------------------------------
# 一次性构造 GPT + FAT16 ESP 镜像
# ---------------------------------------------------------------------------
python3 - "$BUILD" <<'PYEOF'
import os, sys, struct, math, zlib

BUILD = sys.argv[1]

BOOT_EFI = os.path.join(BUILD, "BOOTX64.EFI")
KERNEL_BIN = os.path.join(BUILD, "KERNEL.BIN")
OUT = os.path.join(BUILD, "uefi.img")

boot_efi = open(BOOT_EFI, "rb").read()
kernel   = open(KERNEL_BIN, "rb").read()

# ---------------------------------------------------------------------------
# 基本参数
# ---------------------------------------------------------------------------
BPS = 512
T = 8192                       # 总扇区数 (4 MiB)
PART_START = 2048
PART_END   = 8158
VOL_SECTORS = PART_END - PART_START + 1

SPF = 128
FAT_COUNT = 2
RESERVED = 1
ROOT_ENTRIES = 512
ROOT_SECTORS = math.ceil(ROOT_ENTRIES * 32 / BPS)

# 簇分配
CL_EFI  = 2
CL_BOOT = 3
CL_BX64 = 4
K1 = math.ceil(len(boot_efi) / BPS)
K2 = math.ceil(len(kernel)   / BPS)
CL_KERN = CL_BX64 + K1
LAST_CL = CL_KERN + K2 - 1
AVAIL = VOL_SECTORS - (RESERVED + FAT_COUNT * SPF + ROOT_SECTORS)
assert LAST_CL <= AVAIL + 1, f"ESP too small: need {LAST_CL}, have {AVAIL}"

DATA_LBA = PART_START + RESERVED + FAT_COUNT * SPF + ROOT_SECTORS

# ---------------------------------------------------------------------------
# GUID 工具
# ---------------------------------------------------------------------------
def guid_bytes(g):
    """按 UEFI 规范打包：Data1/2/3 小端，Data4/5 原序"""
    h = g.replace("-", "")
    d1 = int(h[0:8], 16)
    d2 = int(h[8:12], 16)
    d3 = int(h[12:16], 16)
    d4 = bytes.fromhex(h[16:20])
    d5 = bytes.fromhex(h[20:32])
    return struct.pack("<IHH", d1, d2, d3) + d4 + d5

# ---------------------------------------------------------------------------
# FAT16 镜像工具
# ---------------------------------------------------------------------------
def make_dir_entry(name, attr, cluster, size):
    up = name.upper()
    stem, ext = (up.split(".", 1) + [""])[:2] if "." in up else (up, "")
    stem = stem[:8].ljust(8)
    ext  = ext[:3].ljust(3)
    e = bytearray(32)
    e[0:11] = (stem + ext).encode("ascii")[:11]
    e[11] = attr
    struct.pack_into("<H", e, 26, cluster)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)

def make_dot_entry(name, cluster):
    e = bytearray(32)
    for i, ch in enumerate(name.encode("ascii")):
        e[i] = ch
    for i in range(len(name), 11):
        e[i] = 0x20
    e[11] = 0x10
    struct.pack_into("<H", e, 26, cluster)
    return bytes(e)

# ---------------------------------------------------------------------------
# 构造镜像
# ---------------------------------------------------------------------------
img = bytearray(T * BPS)

# ---- BPB（位于 PART_START * BPS）----
bpb = bytearray(512)
bpb[0:3] = b"\xEB\x3C\x90"
bpb[3:11] = b"LUMINA.."
struct.pack_into("<H", bpb, 11, BPS)          # bytes/sector
bpb[13] = 1                                   # sec/cluster
struct.pack_into("<H", bpb, 14, RESERVED)     # reserved
bpb[16] = FAT_COUNT
struct.pack_into("<H", bpb, 17, ROOT_ENTRIES)
struct.pack_into("<H", bpb, 19, VOL_SECTORS)
bpb[21] = 0xF8
struct.pack_into("<H", bpb, 22, SPF)
struct.pack_into("<H", bpb, 24, 63)
struct.pack_into("<H", bpb, 26, 255)
struct.pack_into("<I", bpb, 28, PART_START)
bpb[36] = 0x29
struct.pack_into("<I", bpb, 39, 0x4C554D49)
bpb[43:54] = b"LUMINAOS   "
bpb[54:62] = b"FAT16   "
bpb[510:512] = b"\x55\xAA"
bpb_off = PART_START * BPS
img[bpb_off:bpb_off + 512] = bpb

# ---- FAT（两份）----
fat = bytearray(SPF * BPS)
def fat_set(buf, c, v):
    struct.pack_into("<H", buf, c * 2, v)

fat_set(fat, 0, 0xFFF8)
fat_set(fat, 1, 0xFFFF)
fat_set(fat, CL_EFI, 0xFFFF)
fat_set(fat, CL_BOOT, 0xFFFF)
for i in range(K1):
    fat_set(fat, CL_BX64 + i, CL_BX64 + i + 1 if i < K1 - 1 else 0xFFFF)
for i in range(K2):
    fat_set(fat, CL_KERN + i, CL_KERN + i + 1 if i < K2 - 1 else 0xFFFF)

off1 = (PART_START + RESERVED) * BPS
off2 = (PART_START + RESERVED + SPF) * BPS
img[off1:off1 + len(fat)] = fat
img[off2:off2 + len(fat)] = fat

# ---- 根目录 ----
root_off = (PART_START + RESERVED + FAT_COUNT * SPF) * BPS
root_dir = bytearray(ROOT_SECTORS * BPS)
root_dir[0:32]  = make_dir_entry("EFI",        0x10, CL_EFI,  0)
root_dir[32:64] = make_dir_entry("KERNEL.BIN", 0x20, CL_KERN, len(kernel))
img[root_off:root_off + len(root_dir)] = root_dir

# ---- EFI 目录簇 ----
efi_off = (DATA_LBA + (CL_EFI - 2)) * BPS
efi_dir = bytearray(512)
efi_dir[0:32]  = make_dot_entry(".", CL_EFI)
efi_dir[32:64] = make_dot_entry("..", 0)
efi_dir[64:96] = make_dir_entry("BOOT", 0x10, CL_BOOT, 0)
img[efi_off:efi_off + 512] = efi_dir

# ---- BOOT 目录簇 ----
boot_off = (DATA_LBA + (CL_BOOT - 2)) * BPS
boot_dir = bytearray(512)
boot_dir[0:32]  = make_dot_entry(".", CL_BOOT)
boot_dir[32:64] = make_dot_entry("..", CL_EFI)
boot_dir[64:96] = make_dir_entry("BOOTX64.EFI", 0x20, CL_BX64, len(boot_efi))
img[boot_off:boot_off + 512] = boot_dir

# ---- 文件数据 ----
bx64_off = (DATA_LBA + (CL_BX64 - 2)) * BPS
img[bx64_off:bx64_off + len(boot_efi)] = boot_efi
kern_off = (DATA_LBA + (CL_KERN - 2)) * BPS
img[kern_off:kern_off + len(kernel)] = kernel

# ---- MBR ----
mbr = bytearray(512)
# 分区项 1 (offset 446)
mbr[446] = 0x00
mbr[447:450] = b"\x00\x02\x00"   # 起始 CHS
mbr[450] = 0xEE                  # 类型：GPT protective
mbr[451:454] = b"\xFF\xFF\xFF"   # 结束 CHS
struct.pack_into("<I", mbr, 454, 1)
struct.pack_into("<I", mbr, 458, T)
mbr[510:512] = b"\x55\xAA"
img[0:512] = mbr

# ---- GPT ----
ESP_GUID    = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
DISK_GUID   = "D0C1E2F3-AABB-11EE-8C99-005056C00008"
UNIQ_GUID   = "2D56D6A0-9A1E-4A92-A3F0-1D58E02C3D4F"
ENTRY_ARR_SIZE = 128 * 128

entries = bytearray(ENTRY_ARR_SIZE)
entries[0:16]  = guid_bytes(ESP_GUID)
entries[16:32] = guid_bytes(UNIQ_GUID)
struct.pack_into("<Q", entries, 32, PART_START)
struct.pack_into("<Q", entries, 40, PART_END)
name_utf16 = "EFI System Partition".encode("utf-16-le")
entries[56:56 + min(len(name_utf16), 72)] = name_utf16[:72]

entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF

def make_gpt_header(hdr_lba, entries_lba, backup_lba, entries_crc, disk_guid):
    h = bytearray(512)
    h[0:8] = b"EFI PART"
    struct.pack_into("<I", h, 8, 0x00010000)       # revision
    struct.pack_into("<I", h, 12, 92)              # header size
    # CRC32 留 0，稍后回填
    struct.pack_into("<I", h, 16, 0)
    struct.pack_into("<I", h, 20, 0)               # reserved
    struct.pack_into("<Q", h, 24, hdr_lba)
    struct.pack_into("<Q", h, 32, backup_lba)
    struct.pack_into("<Q", h, 40, 34)              # first usable
    struct.pack_into("<Q", h, 48, T - 34)          # last usable
    h[56:72] = guid_bytes(disk_guid)
    struct.pack_into("<Q", h, 72, entries_lba)
    struct.pack_into("<I", h, 80, 128)             # num entries
    struct.pack_into("<I", h, 84, 128)             # entry size
    struct.pack_into("<I", h, 88, entries_crc)

    hdr_crc = zlib.crc32(bytes(h[0:92])) & 0xFFFFFFFF
    struct.pack_into("<I", h, 16, hdr_crc)
    return bytes(h)

hdr = make_gpt_header(1, 2, T - 1, entries_crc, DISK_GUID)
bkp = make_gpt_header(T - 1, T - 33, 1, entries_crc, DISK_GUID)

img[1 * BPS: 1 * BPS + 512]           = hdr
img[2 * BPS: 2 * BPS + ENTRY_ARR_SIZE] = entries
img[(T - 33) * BPS:(T - 33) * BPS + ENTRY_ARR_SIZE] = entries
img[(T - 1) * BPS:(T - 1) * BPS + 512] = bkp

# ---- 写盘 ----
with open(OUT, "wb") as f:
    f.write(img)

print(f"  uefi.img written ({len(img)} bytes)")
print(f"  BOOTX64.EFI: {len(boot_efi)} B")
print(f"  KERNEL.BIN:  {len(kernel)} B")
PYEOF

echo -e "\033[32m=== UEFI build complete ===\033[0m"
echo "  BOOTX64.EFI: $(stat -c %s "$BUILD/BOOTX64.EFI") B"
echo "  KERNEL.BIN:  $(stat -c %s "$BUILD/KERNEL.BIN") B"
echo "  ESP:         $BUILD/uefi.img"