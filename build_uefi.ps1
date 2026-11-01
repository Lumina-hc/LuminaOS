param()

$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot
$BUILD = "$ROOT\build64"
$EFI_DIR = "$ROOT\boot\efi"
$TOOLCHAIN = "D:\programs\x86_64-elf-tools-windows\bin"
$MINGW_BIN = "D:\programs\msys2\mingw64\bin"
$LLVM_BIN = "D:\programs\LLVM\bin"

$env:PATH = "$TOOLCHAIN;$MINGW_BIN;$LLVM_BIN;$env:PATH"

function Invoke-Tool {
    param([string]$Exe, [string[]]$ToolArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Exe @ToolArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "$Exe failed with exit code $code" }
}

function Get-SymbolAddr {
    param([string]$Name)
    $nm = & "x86_64-elf-nm" "$BUILD\kernel.elf"
    foreach ($line in $nm) {
        if ($line -match "^\s*([0-9a-fA-F]+)\s+[TDBbAa]\s+$Name$") {
            return [Convert]::ToUInt64($matches[1], 16)
        }
        if ($line -match "^([0-9a-fA-F]+)\s+[TDBbAa]\s+$Name$") {
            return [Convert]::ToUInt64($matches[1], 16)
        }
    }
    throw "symbol $Name not found in kernel.elf"
}

function Get-Crc32 {
    param([byte[]]$Data)
    $crc = [uint32]::MaxValue
    foreach ($b in $Data) {
        $crc = $crc -bxor [uint32]$b
        for ($k = 0; $k -lt 8; $k++) {
            if (($crc -band 1) -eq 1) {
                $crc = ($crc -shr 1) -bxor 0xEDB88320
            } else {
                $crc = $crc -shr 1
            }
        }
    }
    return $crc -bxor 0xFFFFFFFF
}

function Write-Int16 {
    param([byte[]]$Buf, [int]$Off, [int]$V)
    $Buf[$Off] = $V -band 0xFF
    $Buf[$Off + 1] = ($V -shr 8) -band 0xFF
}

function Write-Int32 {
    param([byte[]]$Buf, [int]$Off, [int]$V)
    for ($i = 0; $i -lt 4; $i++) { $Buf[$Off + $i] = ($V -shr ($i * 8)) -band 0xFF }
}

function Write-Int64 {
    param([byte[]]$Buf, [int]$Off, [long]$V)
    for ($i = 0; $i -lt 8; $i++) { $Buf[$Off + $i] = ($V -shr ($i * 8)) -band 0xFF }
}

function Write-Guid {
    param([byte[]]$Buf, [int]$Off, [string]$Hex)
    $Hex = $Hex -replace '-', ''
    $b = [byte[]]::new(16)
    for ($i = 0; $i -lt 16; $i++) { $b[$i] = [Convert]::ToByte($Hex.Substring($i * 2, 2), 16) }
    # Data1, Data2, Data3 little-endian per UEFI spec
    [array]::Copy($b, 0, $Buf, $Off, 4)
    [array]::Copy($b, 4, $Buf, $Off + 4, 2)
    [array]::Copy($b, 6, $Buf, $Off + 6, 2)
    [array]::Copy($b, 8, $Buf, $Off + 8, 8)
}

function Set-FatEntry {
    param([byte[]]$Fat, [int]$Cluster, [int]$Next)
    $o = $Cluster * 2
    $Fat[$o] = $Next -band 0xFF
    $Fat[$o + 1] = ($Next -shr 8) -band 0xFF
}

function New-DirEntry {
    param([string]$FileName, [byte]$Attr, [int]$Cluster, [long]$Size)
    $e = [byte[]]::new(32)
    $name = $FileName.ToUpper()
    $dot = $name.IndexOf('.')
    if ($dot -ge 0) {
        $stem = $name.Substring(0, $dot)
        $ext = $name.Substring($dot + 1)
    } else {
        $stem = $name
        $ext = ""
    }
    if ($stem.Length -gt 8) { $stem = $stem.Substring(0, 8) }
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    $n = [System.Text.Encoding]::ASCII.GetBytes(($stem.PadRight(8) + $ext.PadRight(3)).Substring(0, 11))
    [array]::Copy($n, 0, $e, 0, 11)
    $e[11] = $Attr
    $e[26] = $Cluster -band 0xFF
    $e[27] = ($Cluster -shr 8) -band 0xFF
    if ($Size -gt 0) { Write-Int32 $e 28 ([int]$Size) }
    return $e
}

function New-DotEntry {
    param([string]$Name, [int]$Cluster)
    $e = [byte[]]::new(32)
    $c = $Name.ToCharArray()
    for ($i = 0; $i -lt $c.Count; $i++) { $e[$i] = [byte][char]$c[$i] }
    for ($i = $c.Count; $i -lt 11; $i++) { $e[$i] = 0x20 }
    $e[11] = 0x10
    $e[26] = $Cluster -band 0xFF
    $e[27] = ($Cluster -shr 8) -band 0xFF
    return $e
}

function New-EspImage {
    param([string]$Path, [byte[]]$BootEfi, [byte[]]$Kernel)

    $bps = 512
    $T = 8192
    $partStart = 2048
    $partEnd = 8158
    $volSectors = $partEnd - $partStart + 1

    $spf = 128
    $fatCount = 2
    $reserved = 1
    $rootEntries = 512
    $rootSectors = [math]::Ceiling($rootEntries * 32 / $bps)
    $dataLba = $partStart + $reserved + ($fatCount * $spf) + $rootSectors

    $k1 = [math]::Ceiling($BootEfi.Length / $bps)
    $k2 = [math]::Ceiling($Kernel.Length / $bps)
    $clEfi = 2
    $clBoot = 3
    $clBx64 = 4
    $clKern = $clBx64 + $k1
    $lastCl = $clKern + $k2 - 1
    $availClusters = $volSectors - ($reserved + $fatCount * $spf + $rootSectors)
    if ($lastCl -gt ($availClusters + 1)) {
        throw "ESP image too small (need up to cluster $lastCl, $availClusters available)"
    }

    $img = [System.IO.File]::Create($Path)
    $img.SetLength($T * $bps)

    # --- FAT16 BPB -------------------------------------------------------
    $bpb = [byte[]]::new(512)
    $bpb[0] = 0xEB; $bpb[1] = 0x3C; $bpb[2] = 0x90
    $oem = [System.Text.Encoding]::ASCII.GetBytes("LUMINA...")
    [array]::Copy($oem, 0, $bpb, 3, 8)
    $bpb[11] = $bps -band 0xFF
    $bpb[12] = ($bps -shr 8) -band 0xFF
    $bpb[13] = 1
    $bpb[14] = $reserved
    $bpb[16] = $fatCount
    Write-Int16 $bpb 17 $rootEntries
    Write-Int16 $bpb 19 ([int]$volSectors)
    $bpb[21] = 0xF8
    Write-Int16 $bpb 22 $spf
    Write-Int16 $bpb 24 63
    Write-Int16 $bpb 26 255
    Write-Int32 $bpb 28 $partStart
    $bpb[36] = 0x29
    Write-Int32 $bpb 39 0x4C554D49
    [array]::Copy([System.Text.Encoding]::ASCII.GetBytes("LUMINAOS   "), 0, $bpb, 43, 11)
    [array]::Copy([System.Text.Encoding]::ASCII.GetBytes("FAT16   "), 0, $bpb, 54, 8)
    $bpb[510] = 0x55; $bpb[511] = 0xAA

    # --- FAT -------------------------------------------------------------
    $fatSize = $spf * $bps
    $fat = [byte[]]::new($fatSize)
    $fat[0] = 0xF8; $fat[1] = 0xFF
    $fat[2] = 0xFF; $fat[3] = 0xFF
    Set-FatEntry $fat $clEfi 0xFFFF
    Set-FatEntry $fat $clBoot 0xFFFF
    for ($i = 0; $i -lt $k1; $i++) {
        $next = if ($i -lt $k1 - 1) { $clBx64 + $i + 1 } else { 0xFFFF }
        Set-FatEntry $fat ($clBx64 + $i) $next
    }
    for ($i = 0; $i -lt $k2; $i++) {
        $next = if ($i -lt $k2 - 1) { $clKern + $i + 1 } else { 0xFFFF }
        Set-FatEntry $fat ($clKern + $i) $next
    }

    # --- directory content ------------------------------------------------
    $entryEfiDir = New-DirEntry "EFI" 0x10 $clEfi 0
    $entryKern = New-DirEntry "KERNEL.BIN" 0x20 $clKern $Kernel.Length

    # EFI directory cluster: . .. BOOT
    $dirEfi = [byte[]]::new(512)
    $d = New-DotEntry "." $clEfi
    [array]::Copy($d, 0, $dirEfi, 0, 32)
    $d = New-DotEntry ".." 0
    [array]::Copy($d, 0, $dirEfi, 32, 32)
    $entryBootDir = New-DirEntry "BOOT" 0x10 $clBoot 0
    [array]::Copy($entryBootDir, 0, $dirEfi, 64, 32)

    # BOOT directory cluster: . .. BOOTX64.EFI
    $dirBoot = [byte[]]::new(512)
    $d = New-DotEntry "." $clBoot
    [array]::Copy($d, 0, $dirBoot, 0, 32)
    $d = New-DotEntry ".." $clEfi
    [array]::Copy($d, 0, $dirBoot, 32, 32)
    $entryApp = New-DirEntry "BOOTX64.EFI" 0x20 $clBx64 $BootEfi.Length
    [array]::Copy($entryApp, 0, $dirBoot, 64, 32)

    # --- root directory ----------------------------------------------------
    $rootSector = $partStart + $reserved + ($fatCount * $spf)
    $root = [byte[]]::new($rootSectors * $bps)
    [array]::Copy($entryEfiDir, 0, $root, 0, 32)
    [array]::Copy($entryKern, 0, $root, 32, 32)

    # --- write image -------------------------------------------------------
    $img.Position = $partStart * $bps
    $img.Write($bpb, 0, 512)
    $img.Position = ($partStart + 1) * $bps
    $img.Write($fat, 0, $fatSize)
    $img.Position = ($partStart + 1 + $spf) * $bps
    $img.Write($fat, 0, $fatSize)
    $img.Position = $rootSector * $bps
    $img.Write($root, 0, $root.Length)

    # EFI dir cluster and BOOT dir cluster
    $img.Position = ($dataLba + ($clEfi - 2)) * $bps
    $img.Write($dirEfi, 0, 512)
    $img.Position = ($dataLba + ($clBoot - 2)) * $bps
    $img.Write($dirBoot, 0, 512)

    # BOOTX64.EFI data
    $img.Position = ($dataLba + ($clBx64 - 2)) * $bps
    $img.Write($BootEfi, 0, $BootEfi.Length)
    # KERNEL.BIN data
    $img.Position = ($dataLba + ($clKern - 2)) * $bps
    $img.Write($Kernel, 0, $Kernel.Length)

    # --- GPT ----------------------------------------------------------------
    $mbr = [byte[]]::new(512)
    $mbr[446] = 0x00
    $mbr[450] = 0xEE
    Write-Int32 $mbr 454 1
    Write-Int32 $mbr 458 $T
    $mbr[510] = 0x55; $mbr[511] = 0xAA

    $espGuid = "C12A7328F81F11D2BA4B00A0C93EC93B"
    $diskGuid = "D0C1E2F3AABB11EE8C99005056C00008"
    $uniqGuid = "2D56D6A09A1E4A92A3F01D58E02C3D4F"
    $entryArrSize = 128 * 128
    $entries = [byte[]]::new($entryArrSize)
    Write-Guid $entries 0 $espGuid
    Write-Guid $entries 16 $uniqGuid
    Write-Int64 $entries 32 $partStart
    Write-Int64 $entries 40 $partEnd
    $name = [System.Text.Encoding]::Unicode.GetBytes("EFI System Partition")
    [array]::Copy($name, 0, $entries, 56, [Math]::Min($name.Length, 72))
    $entriesCrc = Get-Crc32 $entries

    function New-GptHeader {
        param([int]$HdrLba, [int]$EntriesLba, [int]$BackupLba, [byte[]]$Entries, [uint32]$EntriesCrc, [string]$DiskGuid)
        $h = [byte[]]::new(512)
        [array]::Copy([System.Text.Encoding]::ASCII.GetBytes("EFI PART"), 0, $h, 0, 8)
        $h[8] = 0x00; $h[9] = 0x00; $h[10] = 0x01; $h[11] = 0x00
        $h[12] = 92
        Write-Int64 $h 24 $HdrLba
        Write-Int64 $h 32 $BackupLba
        Write-Int64 $h 40 34
        Write-Int64 $h 48 ($T - 34)
        Write-Guid $h 56 $DiskGuid
        Write-Int64 $h 72 $EntriesLba
        Write-Int32 $h 80 128
        Write-Int32 $h 84 128
        $h[88] = $EntriesCrc -band 0xFF
        $h[89] = ($EntriesCrc -shr 8) -band 0xFF
        $h[90] = ($EntriesCrc -shr 16) -band 0xFF
        $h[91] = ($EntriesCrc -shr 24) -band 0xFF
        $h[16] = 0; $h[17] = 0; $h[18] = 0; $h[19] = 0
        $crc = Get-Crc32 ($h[0..91])
        $h[16] = $crc -band 0xFF
        $h[17] = ($crc -shr 8) -band 0xFF
        $h[18] = ($crc -shr 16) -band 0xFF
        $h[19] = ($crc -shr 24) -band 0xFF
        return $h
    }

    $hdr = New-GptHeader 1 2 ($T - 1) $entries $entriesCrc $diskGuid
    $bkp = New-GptHeader ($T - 1) ($T - 33) 1 $entries $entriesCrc $diskGuid

    $img.Position = 0
    $img.Write($mbr, 0, 512)
    $img.Position = 1 * $bps
    $img.Write($hdr, 0, 512)
    $img.Position = 2 * $bps
    $img.Write($entries, 0, $entryArrSize)
    $img.Position = ($T - 33) * $bps
    $img.Write($entries, 0, $entryArrSize)
    $img.Position = ($T - 1) * $bps
    $img.Write($bkp, 0, 512)

    $img.Flush()
    $img.Close()
    $img.Dispose()
}

Write-Host "=== LuminaOS UEFI Build ===" -ForegroundColor Cyan

if (-not (Test-Path "$BUILD\kernel.elf")) {
    Write-Host "  Kernel not built, running build.ps1 first..."
    & "$ROOT\build.ps1"
}

$entry = Get-SymbolAddr "efi_entry"
$bssStart = Get-SymbolAddr "bss_start"
$bssEnd = Get-SymbolAddr "bss_end"
Write-Host ("  efi_entry=0x{0:X}  bss=0x{1:X}..0x{2:X}" -f $entry, $bssStart, $bssEnd)

[System.IO.Directory]::CreateDirectory($BUILD) | Out-Null

Write-Host "[1/3] Compiling EFI loader..."
$defEntry = "0x{0:X}" -f $entry
$defBssStart = "0x{0:X}" -f $bssStart
$defBssEnd = "0x{0:X}" -f $bssEnd
$loaderFlags = @(
    "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-mno-red-zone", "-mno-stack-arg-probe", "-Wall", "-Wextra", "-O2",
    "-I$EFI_DIR",
    "-DKERNEL_ENTRY=$defEntry",
    "-DKERNEL_BSS_START=$defBssStart",
    "-DKERNEL_BSS_END=$defBssEnd",
    "-c", "$EFI_DIR\efi_loader.c", "-o", "$BUILD\efi_loader.o"
)
Invoke-Tool "x86_64-w64-mingw32-gcc" $loaderFlags

Write-Host "[2/3] Linking BOOTX64.EFI..."
Invoke-Tool "lld-link" @(
    "/out:$BUILD\BOOTX64.EFI",
    "/entry:efi_main",
    "/subsystem:efi_application",
    "/nodefaultlib",
    "/base:0x200000",
    "$BUILD\efi_loader.o"
)

Write-Host "[3/3] Creating uefi.img (GPT ESP)..."
$bootEfi = [System.IO.File]::ReadAllBytes("$BUILD\BOOTX64.EFI")
$kernel = [System.IO.File]::ReadAllBytes("$BUILD\KERNEL.BIN")
New-EspImage "$BUILD\uefi.img" $bootEfi $kernel

Write-Host "=== UEFI build complete ===" -ForegroundColor Green
Write-Host "  BOOTX64.EFI: $($bootEfi.Length) B"
Write-Host "  KERNEL.BIN:  $($kernel.Length) B"
Write-Host "  ESP:         $BUILD\uefi.img"