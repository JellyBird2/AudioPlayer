# AudioPlayer

Windows audio player + any-to-any converter (WAV, FLAC, APE, MP3, Ogg Vorbis,
Opus). One exe: Dear ImGui GUI when launched normally, CLI verbs
(`--to`, `--to-ape`, `--info`, `--device-list`, `--help`) when given flags.

## Prereqs (WSL)

Ubuntu WSL with MinGW-w64, CMake, make, UPX:

```bash
sudo apt install mingw-w64 cmake make upx
```

## Build (order matters)

```bash
# 1. codec static libs (skip if third_party/codecs/install is populated)
third_party/codecs/build-deps.sh

# 2. Monkey's Audio static lib (mac_sdk/ is the trimmed build subset of the
#    official SDK; needs the wincompat Windows.h shim on the include path)
cmake -S mac_sdk -B build-mac -DCMAKE_TOOLCHAIN_FILE="$PWD/mingw-x64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=OFF -DBUILD_UTIL=OFF \
  -DCMAKE_CXX_FLAGS="-I$PWD/wincompat" -DCMAKE_C_FLAGS="-I$PWD/wincompat"
cmake --build build-mac -j"$(nproc)"

# 3. the app (incremental + parallel, UPX-packed)
./build-win.sh imgui   # -> build/AudioPlayer.exe
```

## Layout

- `src/` — app code: `encode*.cpp` (conversion pipeline, everything decodes
  to 16-bit frames), `cli.cpp` (console UI), `win_imgui.cpp` (ImGui GUI),
  `win_player.*` (miniaudio playback + SoundTouch time-stretch).
- `third_party/` — imgui, miniaudio.h, SoundTouch, codec sources + `install/`
  (prebuilt by `build-deps.sh`).
- `mac_sdk/` — trimmed Monkey's Audio SDK (decode/encode core only).
- `wincompat/`, `mingw-x64.cmake` — MinGW cross-build shims.
- `tool/make_icon.py` — regenerates the app icon (`src/AudioPlayer.ico`).

## Verify

```bash
./build/AudioPlayer.exe --help        # from Windows: use Start-Process, see AGENTS.md
./build/AudioPlayer.exe --info <file>
./build/AudioPlayer.exe --to wav <in.ape>
```
