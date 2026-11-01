param(
    [switch]$Headless,
    [switch]$Trace
)

$build = "$PSScriptRoot\build64"
$esp = "$build\uefi.img"
$hdd = "$build\hdd.img"
$qemu = "D:\programs\qemu\qemu-system-x86_64.exe"
$code = "D:\programs\qemu\share\edk2-x86_64-code.fd"
$vars = "D:\programs\qemu\share\edk2-i386-vars.fd"

if (-not (Test-Path $esp)) {
    Write-Host "Error: $esp not found. Run build_uefi.ps1 first." -ForegroundColor Red
    exit 1
}

$gap = [System.IO.File]::ReadAllBytes($code)
$need = 1024 * 1024 * (1 + [int][math]::Floor(($gap.Length - 1) / (1024 * 1024)))
$varsSize = $need - $gap.Length

$varsCopy = "$build\ovmf_vars.fd"
if (-not (Test-Path $varsCopy)) {
    if (Test-Path $vars) {
        Copy-Item $vars $varsCopy
    } else {
        # Fallback: fresh all-0xFF NVRAM (same size as code image's var region).
        $nv = [byte[]]::new($varsSize)
        for ($i = 0; $i -lt $nv.Length; $i++) { $nv[$i] = 0xFF }
        [System.IO.File]::WriteAllBytes($varsCopy, $nv)
    }
}

Get-Process | Where-Object { $_.ProcessName -match "qemu-system" } | Stop-Process -Force -ErrorAction SilentlyContinue

$qemu_args = @(
    "-drive", "if=pflash,format=raw,readonly=on,file=$code",
    "-drive", "if=pflash,format=raw,file=$varsCopy",
    "-m", "128M",
    "-rtc", "base=localtime",
    "-vga", "std"
)

# hdd.img must stay primary master (kernel ATA reads 0x1F0)
if (Test-Path $hdd) {
    $qemu_args += @("-drive", "file=$hdd,format=raw,if=ide,media=disk")
}
$qemu_args += @("-drive", "file=$esp,format=raw,if=ide,media=disk")

if ($Headless) {
    $qemu_args += @("-display", "none", "-monitor", "tcp:127.0.0.1:5555,server,nowait")
} else {
    $qemu_args += @("-display", "gtk,zoom-to-fit=on,full-screen=on")
}

$qemu_args += @("-debugcon", "file:$build\uefi-debug.log")
$qemu_args += @("-chardev", "file,id=ovmf,path=$build\ovmf-debug.log", "-device", "isa-debugcon,iobase=0x402,chardev=ovmf")

if ($Trace) {
    $qemu_args += @("-d", "cpu_reset,int,in_asm", "-D", "$build\qemu-uefi-trace.log")
}

Write-Host "Starting LuminaOS x86_64 with OVMF (UEFI)..." -ForegroundColor Cyan
$env:SDL_VIDEO_CENTERED = "1"
& $qemu @qemu_args