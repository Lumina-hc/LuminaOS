# PowerShell Build Script Bug Fixes

## Fixed Issues

### 1. Path Separator Compatibility
**Problem**: Used backslash `\` for paths (Windows-only)
**Solution**: Changed all paths to use forward slash `/` for cross-platform compatibility

### 2. Missing Toolchain Checks
**Problem**: Direct tool execution without verifying existence
**Solution**: Added `Test-Path` checks for all critical files and tools

### 3. fsutil Command (Windows-only)
**Problem**: `fsutil` command not available on macOS/Linux
**Solution**: Replaced with `dd` command for cross-platform compatibility

### 4. Silent Command Failures
**Problem**: Commands like `gen_font.ps1` and `fsutil` outputs were silently discarded
**Solution**: Used `-RedirectStandardOutput` and `-RedirectStandardError` to capture errors

### 5. Empty File Detection
**Problem**: Only checked file existence, not file size
**Solution**: Added `((Get-Item $path).Length -gt 0)` to verify files aren't empty

### 6. Application Build Verification
**Problem**: Checked only if ELF file existed, not if build succeeded
**Solution**: Added `if ($LASTEXITCODE -ne 0) { continue }` to verify build result

### 7. Toolchain Environment Path
**Problem**: Hardcoded Windows toolchain path
**Solution**: Kept Windows path as-is (toolchain requirement) but improved path format

## Changes Summary

- Line 7: `D:\programs\...` → `D:/programs/...`
- Line 9: Path separator updated
- Line 74: Include path separator updated
- Line 222, 223, 226: All paths updated
- Line 227: `fsutil` → `dd` (macOS/Linux compatible)
- Line 272, 276: Paths updated, empty file check added
- Line 286: Added `$appBuildResult` and `LASTEXITCODE` check
- Line 299: Floppy path updated

## Build Requirements

**Windows**:
- x86_64-elf-gcc (from x86_64-elf-tools-windows)
- nasm
- x86_64-elf-ld
- x86_64-elf-objcopy
- x86_64-elf-size

**macOS/Linux**:
- nasm
- coreutils (for `dd` command)
- x86_64-elf-gcc (from x86_64-elf-tools-linux)

## Testing

The script will now:
1. Fail fast if required tools are missing
2. Report errors for failed command executions
3. Skip empty files gracefully
4. Verify application builds complete successfully
5. Work across Windows, macOS, and Linux (with appropriate toolchains)
