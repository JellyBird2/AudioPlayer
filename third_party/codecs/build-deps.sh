#!/bin/bash
# Cross-build static codec libs for MinGW-w64 (x64) into codecs/install.
# Run in WSL: ./build-deps.sh [--force]
# Produces: libogg, libvorbis (+vorbisenc/vorbisfile), libFLAC (no Ogg),
#           libopus, libmp3lame. All static, -Os.
set -e
cd "$(dirname "$0")"

PREFIX="$(pwd)/install"
TOOLCHAIN="$(pwd)/mingw-codecs.cmake"
JOBS="$(nproc 2>/dev/null || echo 4)"
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

export CC=x86_64-w64-mingw32-gcc
export CXX=x86_64-w64-mingw32-g++
export CFLAGS="-Os -ffunction-sections -fdata-sections"
export CXXFLAGS="-Os -ffunction-sections -fdata-sections"

have_lib() { [ "$FORCE" -eq 0 ] && [ -f "$PREFIX/lib/$1" ]; }

cmake_build() { # $1=srcdir $2=buildname $3...=extra cmake args
  src="$1"; shift
  name="$1"; shift
  mkdir -p "b-$name"
  cmake -S "$src" -B "b-$name" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED_LIBS=OFF \
    "$@"
  cmake --build "b-$name" -j"$JOBS"
  cmake --install "b-$name"
}

if ! have_lib libogg.a; then
  echo "=== ogg ==="
  cmake_build libogg-1.3.5 ogg
else echo "(ogg up to date)"; fi

if ! have_lib libvorbis.a; then
  echo "=== vorbis ==="
  cmake_build libvorbis-1.3.7 vorbis -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DOGG_LIBRARY="$PREFIX/lib/libogg.a" -DOGG_INCLUDE_DIR="$PREFIX/include"
else echo "(vorbis up to date)"; fi

if ! have_lib libFLAC.a; then
  echo "=== flac ==="
  cmake_build flac-1.4.3 flac \
    -DWITH_OGG=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DWITH_STACK_PROTECTOR=OFF \
    -DCMAKE_PREFIX_PATH="$PREFIX"
else echo "(flac up to date)"; fi

if ! have_lib libopus.a; then
  echo "=== opus ==="
  cmake_build opus-1.5.2 opus
else echo "(opus up to date)"; fi

if ! have_lib libmp3lame.a; then
  echo "=== lame ==="
  pushd lame-3.100 >/dev/null
  [ -f Makefile ] || ./configure --host=x86_64-w64-mingw32 \
    --prefix="$PREFIX" --disable-shared --enable-static \
    --disable-nasm --disable-frontend --disable-gtktest --disable-rpath
  make -j"$JOBS"
  make install
  popd >/dev/null
else echo "(lame up to date)"; fi

echo "=== installed ==="
ls -lh "$PREFIX/lib"
