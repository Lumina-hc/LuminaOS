param()

$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot
$BUILD = "$ROOT\build64"
$SDK = Join-Path (Split-Path $ROOT -Parent) "LuminaOS-SDK"
$TOOLCHAIN = "D:\programs\x86_64-elf-tools-windows\bin"

$env:PATH = "$TOOLCHAIN;D:\programs\nasm;$env:PATH"

function Invoke-Tool {
    param([string]$Exe, [string[]]$ToolArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Exe @ToolArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "$Exe failed with exit code $code" }
}

$ASM_FILES = @(
    "boot.asm",
    "gdt.asm",
    "idt.asm",
    "isr.asm",
    "irq.asm",
    "usermode.asm",
    "syscall.asm"
)

$C_FILES = @(
    "kernel\kernel\tty.c",
    "kernel\kernel\pmm.c",
    "kernel\kernel\heap.c",
    "kernel\kernel\paging.c",
    "kernel\kernel\system.c",
    "kernel\kernel\timer.c",
    "kernel\kernel\sched.c",
    "kernel\kernel\keyboard.c",
    "kernel\kernel\mouse.c",
    "kernel\kernel\ata.c",
    "kernel\kernel\fat16.c",
    "kernel\kernel\gfx.c",
    "kernel\kernel\desktop.c",
    "kernel\kernel\elf.c",
    "kernel\kernel\proc.c",
    "kernel\kernel\syscall.c",
    "kernel\arch\x86_64\gdt.c",
    "kernel\arch\x86_64\idt.c",
    "kernel\arch\x86_64\isr.c",
    "kernel\arch\x86_64\irq.c",
    "kernel\kernel\main.c"
)

Write-Host "=== LuminaOS x86_64 Build ===" -ForegroundColor Cyan

New-Item -ItemType Directory -Path $BUILD -Force | Out-Null

Write-Host "[1/5] Assembling boot sector..."
Invoke-Tool "nasm" @("$ROOT\boot\boot.asm", "-f", "bin", "-o", "$BUILD\boot.bin", "-w+all", "-Wno-reloc-abs-word")

Write-Host "[2/5] Assembling kernel entry..."
$asmObjs = @()
foreach ($f in $ASM_FILES) {
    $src = "$ROOT\kernel\arch\x86_64\$f"
    if (-not (Test-Path $src)) { throw "missing asm: $src" }
    $obj = "$BUILD\asm_" + ($f -replace '\.asm$', '.o')
    Invoke-Tool "nasm" @($src, "-f", "elf64", "-o", $obj)
    $asmObjs += $obj
}

Write-Host "[3/5] Compiling kernel C..."
    $cflags = "-ffreestanding", "-nostdlib", "-Wall", "-Wextra", "-I$ROOT\kernel\include", "-std=c99", "-m64", "-mno-red-zone", "-mgeneral-regs-only", "-mno-mmx", "-mno-80387", "-fno-pic", "-fno-pie", "-O2", "-c"
$cObjs = @()
foreach ($f in $C_FILES) {
    $src = "$ROOT\$f"
    if (-not (Test-Path $src)) { throw "missing c: $src" }
    $obj = "$BUILD\" + ([System.IO.Path]::GetFileNameWithoutExtension($f)) + ".o"
    $gccArgs = $cflags + @($src, "-o", $obj)
    Invoke-Tool "x86_64-elf-gcc" $gccArgs
    $cObjs += $obj
}

Write-Host "[4/5] Linking kernel..."
$objs = @()
foreach ($o in $asmObjs) { $objs += $o }
foreach ($o in $cObjs) { $objs += $o }
$ldArgs = @("-T", "$ROOT\kernel\linker.ld", "-o", "$BUILD\kernel.elf") + $objs
Invoke-Tool "x86_64-elf-ld" $ldArgs
Invoke-Tool "x86_64-elf-objcopy" @("-O", "binary", "$BUILD\kernel.elf", "$BUILD\KERNEL.BIN")
$kernSz = (Get-Item "$BUILD\KERNEL.BIN").Length
Invoke-Tool "x86_64-elf-size" @("$BUILD\kernel.elf")

function Write-Fat16Image {
    param([string]$Image, [int]$TotalSectors, [int]$SectorsPerFat, [int]$RootEntries, [byte]$Media)
    $bps = 512
    $spc = 1
    $reserved = 1
    $fatCount = 2
    $rootSectors = [math]::Ceiling($RootEntries * 32 / $bps)

    $bpb = [byte[]]::new(512)
    $bpb[0] = 0xEB; $bpb[1] = 0x3C; $bpb[2] = 0x90
    $oem = [System.Text.Encoding]::ASCII.GetBytes("LUMINAHD ")
    [array]::Copy($oem, 0, $bpb, 3, 8)
    $bpb[11] = $bps -band 0xFF; $bpb[12] = 0
    $bpb[13] = $spc
    $bpb[14] = $reserved -band 0xFF; $bpb[15] = 0
    $bpb[16] = $fatCount
    $bpb[17] = $RootEntries -band 0xFF; $bpb[18] = ($RootEntries -shr 8) -band 0xFF
    $bpb[19] = $TotalSectors -band 0xFF; $bpb[20] = ($TotalSectors -shr 8) -band 0xFF
    $bpb[21] = $Media
    $bpb[22] = $SectorsPerFat -band 0xFF; $bpb[23] = ($SectorsPerFat -shr 8) -band 0xFF
    $bpb[24] = 63; $bpb[25] = 0
    $bpb[26] = 255; $bpb[27] = 0
    $bpb[510] = 0x55; $bpb[511] = 0xAA

    $fatSize = $SectorsPerFat * $bps
    $fat = [byte[]]::new($fatSize)
    $fat[0] = 0xF8; $fat[1] = 0xFF
    $fat[2] = 0xFF; $fat[3] = 0xFF

    $img = [System.IO.File]::Create($Image)
    $img.Write($bpb, 0, 512)
    $null = $img.Seek($reserved * $bps, [System.IO.SeekOrigin]::Begin)
    $img.Write($fat, 0, $fatSize)
    $null = $img.Seek(($reserved + $SectorsPerFat) * $bps, [System.IO.SeekOrigin]::Begin)
    $img.Write($fat, 0, $fatSize)
    $img.SetLength($TotalSectors * $bps)
    $img.Close()
}

function Write-HddFile {
    param([string]$Image, [string]$Name, [byte[]]$Data)
    $bps = 512; $spc = 1; $reserved = 1; $fatCount = 2; $rootEntries = 512
    $spf = 128
    $rootSectors = [math]::Ceiling($rootEntries * 32 / $bps)
    $dataLba = $reserved + ($fatCount * $spf) + $rootSectors
    $rootLba = $reserved + ($fatCount * $spf)
    $clusters = [math]::Ceiling($Data.Length / ($bps * $spc))
    if ($clusters -lt 1) { $clusters = 1 }
    $fs = [System.IO.File]::Open($Image, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)

    $fat = [byte[]]::new($spf * $bps)
    $null = $fs.Seek($reserved * $bps, [System.IO.SeekOrigin]::Begin)
    $null = $fs.Read($fat, 0, $fat.Length)

    $clusterList = [System.Collections.Generic.List[uint16]]::new()
    $first = 0
    for ($c = 2; $c -lt ($spf * $bps / 2); $c++) {
        $entry = [uint16]($fat[$c * 2] -bor ($fat[$c * 2 + 1] -shl 8))
        if ($entry -eq 0) {
            if ($first -eq 0) { $first = $c }
            $clusterList.Add($c)
            if ($clusterList.Count -ge $clusters) { break }
        }
    }
    if ($clusterList.Count -lt $clusters) { $fs.Close(); throw "hdd.img out of space" }

    for ($i = 0; $i -lt $clusterList.Count; $i++) {
        $next = if ($i -lt $clusterList.Count - 1) { $clusterList[$i + 1] } else { 0xFFF8 }
        $fat[$clusterList[$i] * 2] = $next -band 0xFF
        $fat[$clusterList[$i] * 2 + 1] = ($next -shr 8) -band 0xFF
    }
    $null = $fs.Seek($reserved * $bps, [System.IO.SeekOrigin]::Begin)
    $fs.Write($fat, 0, $fat.Length)
    $null = $fs.Seek(($reserved + $spf) * $bps, [System.IO.SeekOrigin]::Begin)
    $fs.Write($fat, 0, $fat.Length)

    for ($i = 0; $i -lt $clusters; $i++) {
        $off = $dataLba + ($clusterList[$i] - 2) * $spc
        $null = $fs.Seek($off * $bps, [System.IO.SeekOrigin]::Begin)
        $len = $bps * $spc
        if ($i -eq $clusters - 1) { $len = $Data.Length - $i * $len }
        if ($len -lt 0) { $len = 0 }
        if ($len -gt 0) { $fs.Write($Data, $i * ($bps * $spc), $len) }
    }

    $entry = [byte[]]::new(32)
    $baseName = $Name.ToUpper()
    $dotIdx = $baseName.IndexOf('.')
    if ($dotIdx -ge 0) {
        $stem = $baseName.Substring(0, $dotIdx)
        $ext = $baseName.Substring($dotIdx + 1)
    } else {
        $stem = $baseName
        $ext = ""
    }
    if ($stem.Length -gt 8) { $stem = $stem.Substring(0, 8) }
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    $fn = [System.Text.Encoding]::ASCII.GetBytes(($stem + "        ").Substring(0, 8))
    $extB = [System.Text.Encoding]::ASCII.GetBytes(($ext + "   ").Substring(0, 3))
    [array]::Copy($fn, 0, $entry, 0, 8)
    [array]::Copy($extB, 0, $entry, 8, 3)
    $entry[11] = 0x20
    $entry[26] = $first -band 0xFF; $entry[27] = ($first -shr 8) -band 0xFF
    $szB = [BitConverter]::GetBytes([uint32]$Data.Length)
    [array]::Copy($szB, 0, $entry, 28, 4)

    $root = [byte[]]::new($rootSectors * $bps)
    $null = $fs.Seek($rootLba * $bps, [System.IO.SeekOrigin]::Begin)
    $null = $fs.Read($root, 0, $root.Length)
    $entryName = [System.Text.Encoding]::ASCII.GetString($fn) + [System.Text.Encoding]::ASCII.GetString($extB)
    $slot = -1
    $free = -1
    for ($i = 0; $i -lt $rootEntries; $i++) {
        $isFree = $root[$i * 32] -eq 0
        $isDel = $root[$i * 32] -eq 0xE5
        $cur = [System.Text.Encoding]::ASCII.GetString($root, $i * 32, 11)
        if ($cur -eq $entryName) { $slot = $i; break }
        if ($free -lt 0 -and ($isFree -or $isDel)) { $free = $i }
    }
    if ($slot -lt 0) { $slot = $free }
    if ($slot -ge 0) { [array]::Copy($entry, 0, $root, $slot * 32, 32) }
    $null = $fs.Seek($rootLba * $bps, [System.IO.SeekOrigin]::Begin)
    $fs.Write($root, 0, $root.Length)
    $fs.Close()
}

Write-Host "[5/5] Creating boot floppy and data disk..."
$boot = [System.IO.File]::ReadAllBytes("$BUILD\boot.bin")
$kernel = [System.IO.File]::ReadAllBytes("$BUILD\KERNEL.BIN")
$kc = [math]::Ceiling($kernel.Length / 512)

if (Test-Path "$BUILD\floppy.img") { Remove-Item "$BUILD\floppy.img" -Force }
fsutil file createnew "$BUILD\floppy.img" 1474560 > $null
$fs = [System.IO.File]::Open("$BUILD\floppy.img", [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write)
$fs.Write($boot, 0, $boot.Length)

$RESERVED_SECTORS = 1
$SECTORS_PER_FAT = 12
$NUM_FATS = 2
$ROOT_ENTRIES = 224
$ROOT_SECTORS = [math]::Ceiling($ROOT_ENTRIES * 32 / 512)
$root_lba = $RESERVED_SECTORS + ($NUM_FATS * $SECTORS_PER_FAT)
$data_lba = $root_lba + $ROOT_SECTORS

$fat = [byte[]]::new($SECTORS_PER_FAT * 512)
$fat[0] = 0xF8; $fat[1] = 0xFF
$fat[2] = 0xFF; $fat[3] = 0xFF
for ($i = 0; $i -lt $kc; $i++) {
    $c = 2 + $i
    $next = if ($i -lt $kc - 1) { $c + 1 } else { 0xFFF8 }
    $fat[$c * 2] = $next -band 0xFF
    $fat[$c * 2 + 1] = ($next -shr 8) -band 0xFF
}
$null = $fs.Seek($RESERVED_SECTORS * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($fat, 0, $fat.Length)
$null = $fs.Seek(($RESERVED_SECTORS + $SECTORS_PER_FAT) * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($fat, 0, $fat.Length)

$entry = [byte[]]::new(32)
$fn = [byte[]]@(0x4B, 0x45, 0x52, 0x4E, 0x45, 0x4C, 0x20, 0x20, 0x42, 0x49, 0x4E)
[array]::Copy($fn, 0, $entry, 0, $fn.Length)
$entry[11] = 0x20
$entry[26] = 2; $entry[27] = 0
for ($b = 0; $b -lt 4; $b++) { $entry[28 + $b] = ($kernel.Length -shr ($b * 8)) -band 0xFF }
$null = $fs.Seek($root_lba * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($entry, 0, $entry.Length)
$null = $fs.Seek($data_lba * 512, [System.IO.SeekOrigin]::Begin)
$fs.Write($kernel, 0, $kernel.Length)
$fs.Close()

$HDD = "$BUILD\hdd.img"
if (-not (Test-Path $HDD)) {
    Write-Fat16Image $HDD 32768 128 512 0xF8
    Write-Host "  hdd.img created (16 MiB)"
}

$FONT_BIN = "$BUILD\font16.bin"
$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& "$ROOT\tools\gen_font.ps1" "$FONT_BIN"
$ErrorActionPreference = $prev
if (Test-Path $FONT_BIN) {
    Write-HddFile $HDD "FONT16.BIN" ([System.IO.File]::ReadAllBytes($FONT_BIN))
    Write-Host "  FONT16.BIN written"
}

$APPS = @("about", "files", "notepad", "system")
foreach ($app in $APPS) {
    $src = "$SDK\examples\$app.c"
    if (-not (Test-Path $src)) { continue }
    $ErrorActionPreference = "Continue"
    & "$SDK\build_app.ps1" -Source $src -OutputDir "$SDK\out"
    $ErrorActionPreference = $prev
    $elf = "$SDK\out\$app.elf"
    if (Test-Path $elf) {
        $name = $app.ToUpper() + ".ELF"
        Write-HddFile $HDD $name ([System.IO.File]::ReadAllBytes($elf))
        Write-Host "  $name written"
    }
}

Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "  Bootloader:  $($boot.Length) B"
Write-Host "  Kernel:      $kernSz B ($kc clusters)"
Write-Host "  Floppy:      $BUILD\floppy.img"
Write-Host "  Data disk:   $HDD"
