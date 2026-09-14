# AGENTS.md — AudioPlayer (AudioPlayer)

## Layout: source is NOT here
- Real source lives in WSL: `Ubuntu-26.04:/root/AudioPlayer`
  (`\\wsl$\Ubuntu-26.04\root\AudioPlayer`). This Windows dir holds only the
  shipped `AudioPlayer.exe`, `AGENTS.md`, `Icon.ico`, and `Fantasy/`
  (13 `.ape` files — the user's music, also handy as convert/`--info` inputs).
- No git repo, no README, no CI, no test suite. Do not assume version control.

## Accessing WSL files
- Read/edit/write tools accept UNC paths, e.g.
  `\\wsl$\Ubuntu-26.04\root\AudioPlayer\src\encode.cpp`.
- Shell is PowerShell; prefer `wsl --cd /root/AudioPlayer <cmd> <args>`.
  Avoid `wsl -e sh -c '...'` (PowerShell mangles quoting/pipes), pipes to
  Windows cmdlets after `wsl` (e.g. `| tail` runs in PowerShell, not WSL),
  and multi-file `wsl grep` (silently returns nothing — one file per call;
  use the Grep tool on UNC paths instead).

## Build (WSL only)
- `wsl --cd /root/AudioPlayer ./build-win.sh imgui` — the only build mode
  (`clean` just wipes obj/exes). Incremental + parallel; resources relink on
  `win_res.rc`/`win_ids.h`/icon/manifest change. No `classic`/`main.cpp`.
- From scratch: `third_party/codecs/build-deps.sh`, then configure
  `mac_sdk` into `build-mac` (`-DBUILD_SHARED=OFF -DBUILD_UTIL=OFF` plus
  `-I$PWD/wincompat` in C/CXXFLAGS — plain configure builds the DLL
  instead of `libMAC.a` and misses `Windows.h`; see README).
- Output `build/AudioPlayer.exe` is UPX `--ultra-brute --lzma` packed
  (already max). Deploy with `Copy-Item -LiteralPath <wsl path> -Force`,
  but first check `Get-Process AudioPlayer` — the exe is locked while the
  user's GUI session runs.
- UPX-packed fresh exes have vanished from this folder before — suspect
  Defender quarantine if files disappear.

## Running / verifying (Windows)
- The exe is GUI-subsystem with CLI verbs (`--to`, `--to-ape`, `--info`,
  `--device-list`, `--help`, `--wav-bits 16|24`, …). `& exe` does NOT wait
  for GUI apps — always use:
  `Start-Process -FilePath $exe -ArgumentList <args> -Wait -NoNewWindow -RedirectStandardOutput <f> -RedirectStandardError <g> -PassThru`
  (`-PassThru` for `$p.ExitCode`; `$LASTEXITCODE` is NOT set by Start-Process).
- CLI output goes to **stderr**, stdout file is usually empty. Use absolute
  paths in args (Start-Process working dir is unreliable).
- Exit codes: 0 ok, 1 usage/io, 2 invalid input (incl. in==out guard),
  3 audio-device failure, 4 encode failure, 5 cancelled.
- No test runner. Verification = convert/`--info` by hand; use throwaway
  scripts under `%TEMP%\opencode` with inputs copied to temp — never write
  outputs into `Fantasy/` (user's files) and never assume the exe isn't
  running.

## Architecture (`src/`, ~8k lines)
- `encode.cpp` dispatcher → `encode_pcm.cpp` (`PcmSource`: everything
  decodes to 16-bit frames) → `encode_extra.cpp` (FLAC/Vorbis/Opus/MP3
  engines + hand-rolled Ogg-Opus demuxer `OpusPcmSource`) → `cli.cpp`
  (`ConsoleMain`) / `win_imgui.cpp` (ImGui GUI; CLI verbs attach to parent
  console first). `win_player.*` = miniaudio playback + SoundTouch
  time-stretch engine.
- Pipeline is 16-bit end to end; `--wav-bits 24` zero-pads LSB (documented
  in `encode.h`), it does not restore depth.
- Speed (`AudioPlayer::SetSpeed`): accepts any finite float > 0, no upper
  clamp; non-finite/≤0 is ignored. GUI box sits right of the volume slider
  (`%.2f` below 1000, `%.4g` above); CLI `,/.` nudges ±0.1x floored at 0.01.

## UI notes (win_imgui.cpp, hard-earned)
- The old bottom status bar is deleted. `g_statusState`/`SetState()` are
  still written everywhere but displayed nowhere — dead until reused.
- Bottom rows are flush with the window edge: host `WindowPadding.y = 0`
  plus the `s_fillExtra` feedback loop that grows `listH` to consume
  leftover space. Do NOT re-add bottom padding or a fixed `avail.y - N`
  reserve without re-tuning, or the gap comes back.
- Transport symbols are hand-drawn (`SymbolButton` draw-list vectors), not
  font glyphs — the default ImGui font has no media symbols. `IM_PI` is
  internal-only; don't use it in `win_imgui.cpp`.
- Convert modal is fixed-width (no `AlwaysAutoResize`): auto-fit snapped the
  width as progress text changed. File list uses a horizontal scrollbar.

## Gotchas that already bit
- `OpusPcmSource::fillFifo` must drain libogg oldest-first **including at
  EOF** — returning after one packet while pages remain silently truncates
  output to ~0.1 s.
- Our opus encoder packs ~25 packets/page; decoders must handle dense pages.
- Granule math: `m_total = maxGranule - preskip`; last-page granulepos is
  `fed - lookahead`.
- Taskbar name is pinned by explicit `SetCurrentProcessExplicitAppUserModelID(L"AudioPlayer")`
  — a stale "AudioPlayer" label means a stale taskbar pin, not a code bug.
- CMakeCache files hardcode absolute paths (`/root/AudioPlayer/...`):
  `build-mac/`, `build-native/`, `build/` are gitignored and must be
  regenerated after a fresh clone or folder move (see Build).
