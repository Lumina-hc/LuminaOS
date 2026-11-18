# CMake toolchain file for building SDL3 as a static library for LuminaOS.
#
# LuminaOS user apps are freestanding ELF64 binaries: no host libc, no crt,
# no dynamic loader. They are compiled with x86_64-elf-gcc and talk to the
# kernel through an int 0x80 syscall ABI (see third_party/LuminaOS-SDK).
#
# Usage (from the repo root):
#   cmake -S third_party/SDL -B build-sdl \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/lumina-toolchain.cmake
#   cmake --build build-sdl
# Or simply run ./ports/sdl3/build_sdl.sh.

# --- Target description ---------------------------------------------------
# "LuminaOS" is not a CMake-native system name; the generic platform is used
# (no implicit libc / linking feature probes that require a C runtime).
set(CMAKE_SYSTEM_NAME LuminaOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# --- Cross tools ----------------------------------------------------------
set(CMAKE_C_COMPILER   x86_64-elf-gcc)
set(CMAKE_CXX_COMPILER x86_64-elf-g++)
set(CMAKE_AR           x86_64-elf-ar)
set(CMAKE_RANLIB       x86_64-elf-ranlib)
set(CMAKE_OBJCOPY      x86_64-elf-objcopy)
set(CMAKE_NM           x86_64-elf-nm)

# Don't try to link an executable during compiler/feature detection: there is
# no libgcc/libc startup to link against. Static-library probes are enough.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# --- Freestanding compile flags -------------------------------------------
# The compat include dir sits first so our minimal <string.h>, <stdio.h>,
# <errno.h>, ... shadow any system headers. SDL_SLOW_* forces SDL to use its
# own bundled memcpy/memmove/memset instead of libc. HAVE_MMAP=0 makes
# dlmalloc use MORECORE (sbrk) only, which the compat libc backs with a static
# arena. __LUMINA__ selects SDL_PLATFORM_LUMINA and the LUMINA config header.
set(LUMINA_COMPAT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../ports/sdl3/compat/include")
file(TO_CMAKE_PATH "${LUMINA_COMPAT_INCLUDE}" LUMINA_COMPAT_INCLUDE)

# -ffunction-sections/-fdata-sections + --gc-sections at final link drop the
# large amount of SDL code we don't use (unused subsystems pull in a lot via
# cross references); -fno-asynchronous-unwind-tables removes ~100 KiB of
# .eh_frame. Both matter because the LuminaOS ELF loader has size limits.
set(CMAKE_C_FLAGS_INIT
    "-ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone -mno-mmx -mno-80387 -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fdata-sections -Wall -O2 -D__LUMINA__=1 -DSDL_SLOW_MEMCPY -DSDL_SLOW_MEMMOVE -DSDL_SLOW_MEMSET -DHAVE_MMAP=0 -I${LUMINA_COMPAT_INCLUDE}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

# Only search the toolchain's own headers/sysroot, never the host's.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
