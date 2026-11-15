#!/usr/bin/env bash
# Build libSDL3.a for LuminaOS (freestanding x86_64-elf).
#
# Uses SDL's CMake with a LUMIA toolchain file, a hand-written build-config
# header, and the compat libc in ports/sdl3/compat. Output:
#   ports/sdl3/lib/libSDL3.a
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDL="$ROOT/third_party/SDL"
BUILD="$ROOT/build64/sdl3"
OUT="$ROOT/ports/sdl3/lib"

mkdir -p "$OUT" "$BUILD"

echo "=== Configuring SDL3 for LuminaOS ==="
cmake -S "$SDL" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/lumia-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_LIBC=OFF \
  -DSDL_INSTALL=OFF \
  -DSDL_UNINSTALL=OFF \
  -DSDL_TESTS=OFF \
  -DSDL_TEST_LIBRARY=OFF \
  -DSDL_EXAMPLES=OFF \
  -DSDL_LEAN_AND_MEAN=ON \
  -DSDL_ASSEMBLY=OFF \
  -DSDL_AVX=OFF -DSDL_AVX2=OFF -DSDL_AVX512F=OFF \
  -DSDL_SSE=OFF -DSDL_SSE2=OFF -DSDL_SSE3=OFF \
  -DSDL_SSE4_1=OFF -DSDL_SSE4_2=OFF -DSDL_MMX=OFF \
  -DSDL_AUDIO=ON \
  -DSDL_VIDEO=ON \
  -DSDL_RENDER=OFF \
  -DSDL_GPU=OFF \
  -DSDL_CAMERA=ON \
  -DSDL_JOYSTICK=OFF \
  -DSDL_HAPTIC=OFF \
  -DSDL_HIDAPI=OFF \
  -DSDL_SENSOR=OFF \
  -DSDL_DIALOG=OFF \
  -DSDL_TRAY=OFF \
  -DSDL_NOTIFICATION=OFF \
  -DSDL_POWER=ON \
  -DSDL_DUMMYAUDIO=ON \
  -DSDL_DISKAUDIO=OFF \
  -DSDL_DUMMYVIDEO=OFF \
  -DSDL_DUMMYCAMERA=ON \
  -DSDL_OFFSCREEN=OFF \
  -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF \
  -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_COCOA=OFF \
  -DSDL_PTHREADS=OFF

echo "=== Building SDL3-static ==="
cmake --build "$BUILD" --target SDL3-static

echo "=== Collecting libSDL3.a ==="
find "$BUILD" -name 'libSDL3*.a' -print
cp "$BUILD/libSDL3.a" "$OUT/libSDL3.a" 2>/dev/null || \
  cp "$(find "$BUILD" -name 'libSDL3*.a' | head -1)" "$OUT/libSDL3.a"
echo "Wrote $OUT/libSDL3.a"
