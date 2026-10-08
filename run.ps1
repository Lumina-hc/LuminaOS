param(
    [switch]$Headless,
    [switch]$Trace
)

$img = "$PSScriptRoot\build64\floppy.img"
$hdd = "$PSScriptRoot\build64\hdd.img"
if (-not (Test-Path $img)) {
    Write-Host "Error: $img not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

$qemu = "D:\programs\qemu\qemu-system-x86_64.exe"

$qemu_args = @(
    "-drive", "file=$img,format=raw,if=floppy",
    "-boot", "a",
    "-m", "32M"
)

if (Test-Path $hdd) {
    $qemu_args += @("-drive", "file=$hdd,format=raw,if=ide,media=disk")
}

if ($Headless) {
    $qemu_args += "-display", "none"
} else {
    $qemu_args += "-display", "sdl"
}

$qemu_args += @("-debugcon", "file:$PSScriptRoot\build64\debug.log")

if ($Trace) {
    $qemu_args += @("-d", "cpu_reset,int", "-D", "$PSScriptRoot\build64\qemu-trace.log")
}

Write-Host "Starting LuminaOS x86_64 in QEMU..." -ForegroundColor Cyan
& $qemu @qemu_args
