# LuminaOS

**LuminaOS v0.7.5** — a small hobby operating system for **x86-64**, built entirely
from scratch: UEFI bootloader, kernel, filesystem, desktop environment, and
user-space applications compiled to ELF64.

No Linux, no GRUB, no existing kernel code. Everything in this repository is
written for the project itself.

## Features

### Boot

- Custom UEFI application (`BOOTX64.EFI`) that opens the GOP framebuffer,
  exits boot services, and enters 64-bit long mode
- Legacy BIOS path (`boot/boot.asm`) for floppy images
- Both boot paths hand off to the same flat `KERNEL.BIN`

### Kernel

- Physical memory manager (bitmap) and 4-level paging with per-process page tables
- GDT, IDT, exception handlers, PIC remapping, PIT and RTC drivers
- PS/2 keyboard and mouse drivers
- ATA PIO driver with a read/write FAT16 filesystem
- Kernel shell with file, memory, and process commands
- Preemptive scheduler with time slicing, sleeping, yielding, and suspend/resume
- ELF64 loader: each process gets its own PML4 and a 16-page user stack, and runs
  in ring 3

### Desktop

- Lumina Desktop on the UEFI GOP framebuffer (1920x1080 on the default OVMF setup)
- Up to **4 windows tiled in a 2x2 grid**, click to focus, focused window is
  highlighted and receives all keyboard input
- Incremental damage-based repainting: only the affected rectangle is redrawn and
  copied to the framebuffer, so an idle desktop costs no CPU
- Single hardware cursor drawn on top of the framebuffer with no trails
- Taskbar with start pill, terminal launcher, live clock, and minimized windows
- Window chrome with minimize (suspend/resume) and close buttons

### System calls

Graphics (pixel, rect, text, flush, window geometry), keyboard, mouse, sleep,
yield, exit, process spawn, pipe create/read/write, and FAT16 file I/O.

### Applications

Ships with `FILES.ELF`, `NOTEPAD.ELF`, `ABOUT.ELF`, `SYSTEM.ELF`,
`TERMINAL.ELF`, and `WORKER.ELF`, all built from the
[LuminaOS-SDK](https://github.com/Lumina-hc/LuminaOS-SDK).

## Directory Layout

```
boot/                  BIOS bootloader and the UEFI loader (boot/efi)
kernel/arch/x86_64/    Entry, GDT, IDT, IRQ, syscalls, user-mode transitions
kernel/include/        Kernel headers
kernel/kernel/         Core modules: pmm, paging, proc, sched, gfx, desktop,
                       syscall, fat16, ata, pipe, keyboard, mouse, timer
docs/                  GitHub Pages site
build.ps1              Builds bootloader, kernel, floppy, hdd, and SDK apps
build_uefi.ps1         Builds BOOTX64.EFI and the GPT/ESP `uefi.img`
run.ps1                QEMU launcher (legacy BIOS floppy)
run_uefi.ps1           QEMU launcher (UEFI + OVMF)
```

## Requirements

- Windows with PowerShell
- NASM
- `x86_64-elf` cross toolchain (`gcc`, `ld`, `objcopy`, `nm`)
- QEMU (`qemu-system-x86_64`)
- OVMF firmware images (`edk2-x86_64-code.fd`)

## Build

```powershell
.\build.ps1        # kernel, floppy, hdd, and SDK applications
.\build_uefi.ps1   # BOOTX64.EFI + build64\uefi.img
```

Artifacts:

- `build64/KERNEL.BIN` — flat 64-bit kernel image
- `build64/BOOTX64.EFI` — UEFI loader
- `build64/uefi.img` — GPT disk image with an EFI System Partition
- `build64/hdd.img` — 16 MiB FAT16 data disk with the applications
- `build64/floppy.img` — boot floppy for the legacy path

## Run

```powershell
.\run_uefi.ps1             # UEFI, GTK window
.\run_uefi.ps1 -Headless   # no display, HMP monitor on tcp:127.0.0.1:5555
.\run.ps1                  # legacy BIOS path
```

Debug output goes to `build64/uefi-debug.log` (kernel `debugcon`) and
`build64/ovmf-debug.log` (firmware).

## Using the desktop

- Click a desktop icon to launch an application; each new app takes the next free
  tile
- Click any window to give it focus; only the focused window gets keyboard input
- The minimize button suspends the process and leaves a taskbar pill; click the
  pill to resume it in a free tile
- The close button (or `Esc`) exits the process and frees its tile

## Known issues (0.7.5)

- Long-running multi-process workloads (for example the Terminal `pipe` demo,
  which spawns `WORKER.ELF`) can still end in a kernel exception and a frozen
  desktop. The root cause is being investigated; single-window and normal
  multi-window use are unaffected in practice.
- `sys_wait` is a stub, and file descriptors are global rather than per-process.

## Developing applications

Applications are C programs compiled to ELF64 against
[LuminaOS-SDK](https://github.com/Lumina-hc/LuminaOS-SDK).

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
