#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build64"
SDK="$(cd "$ROOT/.." && pwd)/LuminaOS-SDK"
TOOLCHAIN="/d/programs/x86_64-elf-tools-windows/bin"

export PATH="$TOOLCHAIN:/d/programs/nasm:$PATH"

run_tool() {
    local exe="$1"; shift
    "$exe" "$@" || { echo "ERROR: $exe failed (exit $?)" >&2; exit 1; }
}

ASM_FILES=(
    boot.asm gdt.asm idt.asm isr.asm irq.asm usermode.asm syscall.asm
)

C_FILES=(
    kernel/kernel/tty.c
    kernel/kernel/pmm.c
    kernel/kernel/heap.c
    kernel/kernel/paging.c
    kernel/kernel/system.c
    kernel/kernel/timer.c
    kernel/kernel/sched.c
    kernel/kernel/keyboard.c
    kernel/kernel/mouse.c
    kernel/kernel/ata.c
    kernel/kernel/fat16.c
    kernel/kernel/gfx.c
    kernel/kernel/desktop.c
    kernel/kernel/elf.c
    kernel/kernel/proc.c
    kernel/kernel/pipe.c
    kernel/kernel/syscall.c
    users/services/render.c
    users/services/password_services.c
    kernel/arch/x86_64/gdt.c
    kernel/arch/x86_64/idt.c
    kernel/arch/x86_64/isr.c
    kernel/arch/x86_64/irq.c
    kernel/kernel/main.c
)

echo -e "\033[36m=== LuminaOS x86_64 Build ===\033[0m"
mkdir -p "$BUILD"

echo "[1/5] Assembling boot sector..."
run_tool nasm "$ROOT/boot/boot.asm" -f bin -o "$BUILD/boot.bin" -w+all -Wno-reloc-abs-word

echo "[2/5] Assembling kernel entry..."
ASM_OBJS=()
for f in "${ASM_FILES[@]}"; do
    src="$ROOT/kernel/arch/x86_64/$f"
    [[ -f "$src" ]] || { echo "missing asm: $src" >&2; exit 1; }
    obj="$BUILD/asm_${f%.asm}.o"
    run_tool nasm "$src" -f elf64 -o "$obj"
    ASM_OBJS+=("$obj")
done

echo "[3/5] Compiling kernel C..."
CFLAGS=(
    -ffreestanding -nostdlib -Wall -Wextra
    "-I$ROOT/kernel/include"
    "-I$ROOT/users/include"
    -std=c99 -m64 -mno-red-zone -mgeneral-regs-only
    -mno-mmx -mno-80387 -fno-pic -fno-pie -O2 -c
)
C_OBJS=()
for f in "${C_FILES[@]}"; do
    src="$ROOT/$f"
    [[ -f "$src" ]] || { echo "missing c: $src" >&2; exit 1; }
    obj="$BUILD/$(basename "$f" .c).o"
    run_tool x86_64-elf-gcc "${CFLAGS[@]}" "$src" -o "$obj"
    C_OBJS+=("$obj")
done

echo "[4/5] Linking kernel..."
run_tool x86_64-elf-ld -T "$ROOT/kernel/linker.ld" -o "$BUILD/kernel.elf" \
    "${ASM_OBJS[@]}" "${C_OBJS[@]}"
run_tool x86_64-elf-objcopy -O binary "$BUILD/kernel.elf" "$BUILD/KERNEL.BIN"
kernSz=$(stat -c %s "$BUILD/KERNEL.BIN")
run_tool x86_64-elf-size "$BUILD/kernel.elf"

echo "[5/5] Creating boot floppy and data disk..."

# ---------------------------------------------------------------------------
# 用 Python 一次性构造 floppy.img 与 hdd.img（快速、无逐字节 dd）
# ---------------------------------------------------------------------------
python3 - "$BUILD" "$kernSz" <<'PYEOF'
import os, sys, struct, math

BUILD = sys.argv[1]
KERN_SZ = int(sys.argv[2])

def read_file(path):
    with open(path, "rb") as f:
        return f.read()

boot  = read_file(os.path.join(BUILD, "boot.bin"))
kernel = read_file(os.path.join(BUILD, "KERNEL.BIN"))

# ===========================================================================
# 构造 1.44MB FAT12 软盘镜像
# ===========================================================================
def build_floppy(path, boot, kernel):
    bps = 512
    total = 2880              # 1.44 MB
    spc = 1
    reserved = 1
    nfats = 2
    root_entries = 224
    spf = 12
    root_sectors = math.ceil(root_entries * 32 / bps)  # 14
    root_lba = reserved + nfats * spf
    data_lba = root_lba + root_sectors

    kc = math.ceil(len(kernel) / bps)

    img = bytearray(total * bps)

    # --- BPB ---
    bpb = struct.pack(
        "<3s8sHBHBHHBHHHII",
        b"\xEB\x3C\x90",
        b"LUMINA  ",          # 8 字节 OEM
        bps,                 # bytes per sector
        spc,                 # sectors per cluster
        reserved,            # reserved sectors
        nfats,               # num FATs
        root_entries,        # root entries
        total,               # total sectors (16-bit)
        0xF8,                # media
        spf,                 # sectors per FAT
        63,                  # sectors per track
        255,                 # heads
        0,                   # hidden
        0,                   # total sectors 32
    )
    img[0:len(bpb)] = bpb
    img[510:512] = b"\x55\xAA"

    # --- FAT12 ---
    fat = bytearray(spf * bps)
    # FAT[0]=0xFF8, FAT[1]=0xFFF  -> 12-bit 紧凑布局
    # 简单起见逐项按 12-bit 写入辅助
    fat12 = [0] * (spf * bps * 2 // 3)
    fat12[0] = 0xFF8
    fat12[1] = 0xFFF
    for i in range(kc):
        c = 2 + i
        fat12[c] = c + 1 if i < kc - 1 else 0xFFF
    # 打包 12-bit
    raw = bytearray(spf * bps)
    for i, v in enumerate(fat12):
        if v == 0:
            continue
        off = (i * 3) // 2
        if i % 2 == 0:
            raw[off]     = v & 0xFF
            raw[off + 1] = (raw[off + 1] & 0xF0) | ((v >> 8) & 0x0F)
        else:
            raw[off]     = (raw[off] & 0x0F) | ((v << 4) & 0xF0)
            raw[off + 1] = (v >> 4) & 0xFF
    fat_off1 = reserved * bps
    fat_off2 = (reserved + spf) * bps
    img[fat_off1:fat_off1 + len(raw)] = raw
    img[fat_off2:fat_off2 + len(raw)] = raw

    # --- 根目录：KERNEL.BIN ---
    entry = bytearray(32)
    name11 = b"KERNEL  BIN"
    entry[0:11] = name11
    entry[11] = 0x20
    entry[26:28] = struct.pack("<H", 2)          # 起始簇
    entry[28:32] = struct.pack("<I", len(kernel))  # 大小
    root_off = root_lba * bps
    img[root_off:root_off + 32] = entry

    # --- 内核数据 ---
    data_off = data_lba * bps
    img[data_off:data_off + len(kernel)] = kernel

    # --- boot 扇区 ---
    img[0:len(boot)] = boot

    with open(path, "wb") as f:
        f.write(img)

# ===========================================================================
# FAT16 通用写入（hdd.img）
# ===========================================================================
class Fat16:
    def __init__(self, path, bps=512, spc=1, reserved=1, nfats=2,
                 root_entries=512, spf=128):
        self.path = path
        self.bps = bps
        self.spc = spc
        self.reserved = reserved
        self.nfats = nfats
        self.root_entries = root_entries
        self.spf = spf
        self.root_sectors = math.ceil(root_entries * 32 / bps)
        self.root_lba = reserved + nfats * spf
        self.data_lba = self.root_lba + self.root_sectors
        with open(path, "rb") as f:
            self.img = bytearray(f.read())
        self.fat_off = reserved * bps
        self.fat_size = spf * bps

    def _fat_get(self, c):
        o = self.fat_off + c * 2
        return struct.unpack_from("<H", self.img, o)[0]

    def _fat_set(self, c, v):
        for i in range(self.nfats):
            o = (self.reserved + i * self.spf) * self.bps + c * 2
            struct.pack_into("<H", self.img, o, v)

    def _alloc_chain(self, n):
        maxc = self.fat_size // 2
        free = []
        for c in range(2, maxc):
            if self._fat_get(c) == 0:
                free.append(c)
                if len(free) >= n:
                    break
        if len(free) < n:
            raise RuntimeError("hdd.img out of space")
        for i, c in enumerate(free):
            nxt = free[i + 1] if i < len(free) - 1 else 0xFFF8
            self._fat_set(c, nxt)
        return free

    def _write_dir_entry(self, name, first_cluster, size):
        stem, ext = "", ""
        up = name.upper()
        if "." in up:
            stem, ext = up.split(".", 1)
        else:
            stem = up
        stem = stem[:8].ljust(8)
        ext = ext[:3].ljust(3)
        name11 = (stem + ext).encode("ascii")[:11]

        root_off = self.root_lba * self.bps
        slot = None
        free_slot = None
        for i in range(self.root_entries):
            base = root_off + i * 32
            first = self.img[base]
            if first == 0x00 or first == 0xE5:
                if free_slot is None:
                    free_slot = i
                continue
            cur = bytes(self.img[base:base + 11])
            if cur == name11:
                slot = i
                break
        if slot is None:
            slot = free_slot
        if slot is None:
            raise RuntimeError("root dir full")

        e = bytearray(32)
        e[0:11] = name11
        e[11] = 0x20
        struct.pack_into("<H", e, 26, first_cluster)
        struct.pack_into("<I", e, 28, size)
        self.img[root_off + slot * 32: root_off + slot * 32 + 32] = e

    def add_file(self, name, data):
        n = max(1, math.ceil(len(data) / (self.bps * self.spc)))
        chain = self._alloc_chain(n)
        for i, c in enumerate(chain):
            off = (self.data_lba + (c - 2) * self.spc) * self.bps
            chunk = data[i * self.bps * self.spc:(i + 1) * self.bps * self.spc]
            self.img[off:off + len(chunk)] = chunk
        self._write_dir_entry(name, chain[0], len(data))

    def save(self):
        with open(self.path, "wb") as f:
            f.write(self.img)

def make_fat16_image(path, total_sectors=32768, spf=128,
                     root_entries=512, media=0xF8):
    bps = 512
    spc = 1
    reserved = 1
    nfats = 2
    root_sectors = math.ceil(root_entries * 32 / bps)

    img = bytearray(total_sectors * bps)

    bpb = struct.pack(
        "<3s8sHBHBHHBHHHII",
        b"\xEB\x3C\x90",
        b"LUMINAHD",
        bps, spc, reserved, nfats, root_entries,
        total_sectors if total_sectors < 65536 else 0,
        media, spf, 63, 255, 0,
        total_sectors if total_sectors >= 65536 else 0,
    )
    img[0:len(bpb)] = bpb
    img[510:512] = b"\x55\xAA"

    fat = bytearray(spf * bps)
    struct.pack_into("<H", fat, 0, 0xFFF8)
    struct.pack_into("<H", fat, 2, 0xFFFF)
    off1 = reserved * bps
    off2 = (reserved + spf) * bps
    img[off1:off1 + len(fat)] = fat
    img[off2:off2 + len(fat)] = fat

    with open(path, "wb") as f:
        f.write(img)

# ---------------------------------------------------------------------------
# 执行
# ---------------------------------------------------------------------------
build_floppy(os.path.join(BUILD, "floppy.img"), boot, kernel)
print("  floppy.img written")

HDD = os.path.join(BUILD, "hdd.img")
if os.path.exists(HDD):
    os.remove(HDD)
make_fat16_image(HDD)
print("  hdd.img recreated clean (16 MiB)")

# 字体
font_bin = os.path.join(BUILD, "font16.bin")
sys.stdout.flush()
PYEOF

# 字体生成（调用 PowerShell 子脚本；如果你的 gen_font 也移植成了 python，改成 python3）
FONT_BIN="$BUILD/font16.bin"
if command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
        -File "$ROOT/tools/gen_font.ps1" "$FONT_BIN" 2>/dev/null || true
else
    python3 "$ROOT/tools/gen_font.py" "$FONT_BIN" 2>/dev/null || true
fi

# 用 Python 把字体 + 应用 ELF 写入 hdd.img
if [[ -f "$FONT_BIN" ]]; then
    python3 - "$BUILD/hdd.img" "FONT16.BIN" "$FONT_BIN" <<'PYEOF'
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# 内联 Fat16 实现（与上面相同）
import struct, math

class Fat16:
    def __init__(self, path, bps=512, spc=1, reserved=1, nfats=2,
                 root_entries=512, spf=128):
        self.path = path; self.bps = bps; self.spc = spc
        self.reserved = reserved; self.nfats = nfats
        self.root_entries = root_entries; self.spf = spf
        self.root_sectors = math.ceil(root_entries * 32 / bps)
        self.root_lba = reserved + nfats * spf
        self.data_lba = self.root_lba + self.root_sectors
        with open(path, "rb") as f:
            self.img = bytearray(f.read())
        self.fat_off = reserved * bps
        self.fat_size = spf * bps

    def _fat_get(self, c):
        return struct.unpack_from("<H", self.img, self.fat_off + c * 2)[0]

    def _fat_set(self, c, v):
        for i in range(self.nfats):
            struct.pack_into("<H", self.img,
                (self.reserved + i * self.spf) * self.bps + c * 2, v)

    def _alloc_chain(self, n):
        free = [c for c in range(2, self.fat_size // 2)
                if self._fat_get(c) == 0][:n]
        if len(free) < n: raise RuntimeError("out of space")
        for i, c in enumerate(free):
            self._fat_set(c, free[i + 1] if i < len(free) - 1 else 0xFFF8)
        return free

    def add_file(self, name, data):
        n = max(1, math.ceil(len(data) / (self.bps * self.spc)))
        chain = self._alloc_chain(n)
        for i, c in enumerate(chain):
            off = (self.data_lba + (c - 2) * self.spc) * self.bps
            chunk = data[i * self.bps * self.spc:(i + 1) * self.bps * self.spc]
            self.img[off:off + len(chunk)] = chunk

        up = name.upper()
        stem, ext = (up.split(".", 1) + [""])[:2] if "." in up else (up, "")
        name11 = (stem[:8].ljust(8) + ext[:3].ljust(3)).encode("ascii")[:11]

        root_off = self.root_lba * self.bps
        slot = free_slot = None
        for i in range(self.root_entries):
            base = root_off + i * 32
            first = self.img[base]
            if first in (0x00, 0xE5):
                if free_slot is None: free_slot = i
                continue
            if bytes(self.img[base:base + 11]) == name11:
                slot = i; break
        if slot is None: slot = free_slot
        if slot is None: raise RuntimeError("root full")
        e = bytearray(32)
        e[0:11] = name11; e[11] = 0x20
        struct.pack_into("<H", e, 26, chain[0])
        struct.pack_into("<I", e, 28, len(data))
        self.img[root_off + slot * 32:root_off + slot * 32 + 32] = e

    def save(self):
        with open(self.path, "wb") as f:
            f.write(self.img)

img_path, name, src = sys.argv[1:4]
fs = Fat16(img_path)
fs.add_file(name, open(src, "rb").read())
fs.save()
PYEOF
    echo "  FONT16.BIN written"
fi

# ---------------------------------------------------------------------------
# 编译并写入示例应用
# ---------------------------------------------------------------------------
APPS=(about files notepad system terminal worker)
for app in "${APPS[@]}"; do
    src="$SDK/examples/$app.c"
    [[ -f "$src" ]] || continue
    if command -v powershell.exe >/dev/null 2>&1; then
        powershell.exe -NoProfile -ExecutionPolicy Bypass \
            -File "$SDK/build_app.ps1" -Source "$src" -OutputDir "$SDK/out" 2>/dev/null || true
    fi
    elf="$SDK/out/$app.elf"
    [[ -f "$elf" ]] || continue
    name="$(echo "$app" | tr '[:lower:]' '[:upper:]').ELF"
    python3 - "$BUILD/hdd.img" "$name" "$elf" <<'PYEOF'
import sys, struct, math
class Fat16:
    def __init__(self, path, bps=512, spc=1, reserved=1, nfats=2,
                 root_entries=512, spf=128):
        self.path=path; self.bps=bps; self.spc=spc
        self.reserved=reserved; self.nfats=nfats
        self.root_entries=root_entries; self.spf=spf
        self.root_sectors=math.ceil(root_entries*32/bps)
        self.root_lba=reserved+nfats*spf
        self.data_lba=self.root_lba+self.root_sectors
        self.img=bytearray(open(path,"rb").read())
        self.fat_off=reserved*bps; self.fat_size=spf*bps
    def _get(self,c): return struct.unpack_from("<H",self.img,self.fat_off+c*2)[0]
    def _set(self,c,v):
        for i in range(self.nfats):
            struct.pack_into("<H",self.img,
                (self.reserved+i*self.spf)*self.bps+c*2,v)
    def add_file(self,name,data):
        n=max(1,math.ceil(len(data)/(self.bps*self.spc)))
        free=[c for c in range(2,self.fat_size//2) if self._get(c)==0][:n]
        if len(free)<n: raise RuntimeError("out of space")
        for i,c in enumerate(free):
            self._set(c, free[i+1] if i<len(free)-1 else 0xFFF8)
        for i,c in enumerate(free):
            off=(self.data_lba+(c-2)*self.spc)*self.bps
            chunk=data[i*self.bps*self.spc:(i+1)*self.bps*self.spc]
            self.img[off:off+len(chunk)]=chunk
        up=name.upper()
        stem,ext=(up.split(".",1)+[""])[:2] if "." in up else (up,"")
        n11=(stem[:8].ljust(8)+ext[:3].ljust(3)).encode("ascii")[:11]
        ro=self.root_lba*self.bps; slot=free_slot=None
        for i in range(self.root_entries):
            b=ro+i*32; f=self.img[b]
            if f in (0,0xE5):
                if free_slot is None: free_slot=i
                continue
            if bytes(self.img[b:b+11])==n11: slot=i; break
        if slot is None: slot=free_slot
        e=bytearray(32); e[0:11]=n11; e[11]=0x20
        struct.pack_into("<H",e,26,free[0])
        struct.pack_into("<I",e,28,len(data))
        self.img[ro+slot*32:ro+slot*32+32]=e
    def save(self): open(self.path,"wb").write(self.img)
p,name,src=sys.argv[1:4]
fs=Fat16(p); fs.add_file(name,open(src,"rb").read()); fs.save()
PYEOF
    echo "  $name written"
done

echo "Refreshing UEFI boot image..."
bash "$ROOT/build_uefi.sh"

echo -e "\033[32m=== Build complete ===\033[0m"
echo "  Bootloader:  $(stat -c %s "$BUILD/boot.bin") B"
echo "  Kernel:      $kernSz B"
echo "  Floppy:      $BUILD/floppy.img"
echo "  Data disk:   $BUILD/hdd.img"