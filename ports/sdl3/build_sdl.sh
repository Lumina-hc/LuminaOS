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

# The LUMIA platform port is stored as a patch against upstream SDL (fda4ece)
# rather than a fork, so that "git submodule update --init" plus this script
# reproduces the full port from a clean clone. Apply it idempotently: a
# successful reverse-check means the patch is already in the working tree.
# To refresh after editing SDL directly:
#   git -C third_party/SDL add -A
#   git -C third_party/SDL diff --cached --binary > ports/sdl3/patches/0001-lumia-port.patch
#   git -C third_party/SDL reset
LUMIA_PATCH="$ROOT/ports/sdl3/patches/0001-lumia-port.patch"
if [[ ! -f "$SDL/CMakeLists.txt" ]]; then
    echo "SDL submodule is not initialized. Run:" >&2
    echo "  git submodule update --init third_party/SDL" >&2
    exit 1
fi
if [[ -f "$LUMIA_PATCH" ]]; then
    if ! git -C "$SDL" apply --check --reverse "$LUMIA_PATCH" >/dev/null 2>&1; then
        echo "=== Applying SDL3 LUMIA port patch ==="
        git -C "$SDL" apply "$LUMIA_PATCH" || {
            echo "Failed to apply $LUMIA_PATCH." >&2
            echo "Ensure third_party/SDL is at fda4ece with no conflicting edits." >&2
            exit 1
        }
        echo "=== SDL3 LUMIA port patch applied ==="
    fi
fi

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
