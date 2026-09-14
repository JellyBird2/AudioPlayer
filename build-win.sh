#!/bin/bash
# Build AudioPlayer for Windows (x64) with MinGW-w64. Run in WSL.
# Usage: ./build-win.sh [imgui|clean]   (default: imgui)
# Parallel + incremental: only stale TUs recompile, up to nproc at once.
set -e
set -m # enable job control so `jobs`/`wait -n` throttle parallel compiles
cd "$(dirname "$0")"

MODE="${1:-imgui}"
CXX="x86_64-w64-mingw32-g++"
STD="-std=c++17 -Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections"
DEFS="-DUNICODE -D_UNICODE -DPLATFORM_WINDOWS -DFLAC__NO_DLL -DST_NO_EXCEPTION_HANDLING"
INC="-Isrc -Ithird_party -Ithird_party/imgui -Iwincompat -Imac_sdk/Shared -Imac_sdk/Source/Shared -Imac_sdk/Source/MACLib -Ithird_party/codecs/install/include"
LIBS="-municode -mwindows -Wl,--gc-sections -lcomctl32 -lshell32 -lole32 -luuid -lwindowscodecs -lwinmm -lcomdlg32 -static-libgcc -static-libstdc++"
CODECLIB="third_party/codecs/install/lib/libFLAC.a third_party/codecs/install/lib/libvorbisenc.a third_party/codecs/install/lib/libvorbisfile.a third_party/codecs/install/lib/libvorbis.a third_party/codecs/install/lib/libogg.a third_party/codecs/install/lib/libopus.a third_party/codecs/install/lib/libmp3lame.a"
MACLIB="build-mac/libMAC.a"
NJOBS="$(nproc 2>/dev/null || echo 4)"

if [ "$MODE" = "clean" ]; then
  rm -rf build/obj-* build/AudioPlayer*.exe build/AudioPlayer_*.exe build/opt*.exe build/baseline_*.exe
  echo "cleaned"
  exit 0
fi

if [ "$MODE" = "imgui" ]; then
  SRC="src/cli.cpp src/encode.cpp src/encode_pcm.cpp src/encode_extra.cpp src/id3.cpp src/win_player.cpp src/win_art.cpp src/win_imgui.cpp third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_win32.cpp third_party/imgui/backends/imgui_impl_dx11.cpp third_party/soundtouch/SoundTouch.cpp third_party/soundtouch/TDStretch.cpp third_party/soundtouch/RateTransposer.cpp third_party/soundtouch/FIRFilter.cpp third_party/soundtouch/FIFOSampleBuffer.cpp third_party/soundtouch/AAFilter.cpp third_party/soundtouch/cpu_detect_x86.cpp third_party/soundtouch/InterpolateLinear.cpp third_party/soundtouch/InterpolateCubic.cpp third_party/soundtouch/InterpolateShannon.cpp third_party/soundtouch/sse_optimized.cpp"
  LIBS="$LIBS -ld3d11 -ldxgi -ld3dcompiler -ldwmapi"
  OUT="build/AudioPlayer.exe"
else
  echo "Usage: $0 [imgui|clean]" >&2
  exit 1
fi

OBJDIR="build/obj-$MODE"
mkdir -p "$OBJDIR" build

# Compile Windows resources (icon, manifest, version, dialogs) when stale.
# windres tracks win_res.rc only, so also watch the headers/icon it includes.
RES="$OBJDIR/win_res.o"
if [ ! -f "$RES" ] || [ src/win_res.rc -nt "$RES" ] || [ src/win_ids.h -nt "$RES" ] || \
    [ src/AudioPlayer.ico -nt "$RES" ] || [ src/win_manifest.xml -nt "$RES" ]; then
  echo " RES src/win_res.rc"
  x86_64-w64-mingw32-windres -Isrc -o "$RES" src/win_res.rc
fi

# True (0) when $2 is missing or older than $1 or any header dep.
needs_build() {
  [ ! -f "$2" ] && return 0
  [ "$1" -nt "$2" ] && return 0
  dep="${2%.o}.d"
  if [ -f "$dep" ]; then
    # $dep is "obj: src hdr hdr ...\" with backslash-newline continuations
    for f in $(sed -e 's/^[^:]*://' -e 's/\\//g' "$dep"); do
      [ -e "$f" ] && [ "$f" -nt "$2" ] && return 0
    done
  fi
  return 1
}

PIDS=""
FAIL=0
for src in $SRC; do
  obj="$OBJDIR/${src//\//_}"
  obj="${obj%.cpp}.o"
  # shellcheck disable=SC2086
  if needs_build "$src" "$obj"; then
    echo " CC $src"
    EXTRA=""
    case "$src" in
      *sse_optimized.cpp) EXTRA="-msse" ;; # SoundTouch SIMD FIR (upstream builds it with -msse)
    esac
    # shellcheck disable=SC2086
    $CXX $STD $DEFS $INC $EXTRA -MMD -MP -c "$src" -o "$obj" &
    PIDS="$PIDS $!"
    while [ "$(jobs -rp | wc -l)" -ge "$NJOBS" ]; do
      wait -n || FAIL=1
    done
  fi
done
for pid in $PIDS; do
  wait "$pid" || FAIL=1
done
[ "$FAIL" -ne 0 ] && echo "compile failed" >&2 && exit 1

OBJS=""
for src in $SRC; do
  obj="$OBJDIR/${src//\//_}"
  OBJS="$OBJS ${obj%.cpp}.o"
done

LINK=0
[ ! -f "$OUT" ] && LINK=1
if [ "$LINK" -eq 0 ]; then
  for o in $OBJS; do
    [ "$o" -nt "$OUT" ] && LINK=1 && break
  done
  [ "$RES" -nt "$OUT" ] && LINK=1
  [ "$MACLIB" -nt "$OUT" ] && LINK=1
fi

if [ "$LINK" -eq 1 ]; then
  echo " LINK $OUT"
  # shellcheck disable=SC2086
  $CXX $STD $DEFS $INC $OBJS "$RES" $MACLIB $CODECLIB -o "$OUT" $LIBS
  x86_64-w64-mingw32-strip "$OUT"
  if command -v upx >/dev/null 2>&1; then
    upx --ultra-brute --lzma "$OUT"
  fi
else
  echo "up to date: $OUT"
fi
ls -lh "$OUT"
x86_64-w64-mingw32-objdump -p "$OUT" | grep "DLL Name" | sort -u
