// AudioPlayer v2 - audio player + converter for Windows.
// Built in WSL with MinGW-w64. Official MAC SDK (APE decode/encode),
// miniaudio (playback + WAV), libFLAC, LAME (MP3), libvorbis, libopus.
//
// Usage:
//   AudioPlayer.exe [files/dirs]            Play queue (picker if none)
//   AudioPlayer.exe --info <files...>       Show info only
//   AudioPlayer.exe --to <fmt> <in...> [--flac-level N] [--quality Q]
//                                          [--bitrate kbps] [--threads N] [-t "A=B|.."]
//                                          Convert audio, any supported in -> any out
//   AudioPlayer.exe --device-list           List audio devices
//   AudioPlayer.exe --help                  Help
//   Flags: --no-color, --ascii
//
// Keys while playing:
//   Space pause, Q/Esc quit, Left/Right seek, Up/Down vol, I info, M mute,
//   N next, P previous, L loop mode, S shuffle

#include "ma_config.h" // MINIAUDIO_IMPLEMENTATION lives in win_player.cpp
#include "miniaudio.h"

#include "ui.h"
#include "encode.h"
#include "id3.h"
#include "win_player.h"

#include <windows.h>
#include <commdlg.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <wchar.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <atomic>

// ---------------------------------------------------------------- state
struct Session {
    std::vector<std::wstring> tracks;
    std::vector<int> order;
    size_t pos = 0;
    int loop = 0; // 0 off, 1 all, 2 one
    bool shuffle = false;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> muted{false};
    std::atomic<float> speed{1.0f};
};

// ---------------------------------------------------------------- helpers
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::string WideToUtf8(const wchar_t* w) {
    return w ? WideToUtf8(std::wstring(w)) : std::string();
}

static void PrintTagField(const std::wstring& v, const char* label) {
    if (!v.empty()) printf("  %-10s: %s\n", label, WideToUtf8(v.c_str()).c_str());
}

static std::wstring BaseName(const std::wstring& path) {
    size_t sep = path.find_last_of(L"\\/:");
    return sep == std::wstring::npos ? path : path.substr(sep + 1);
}

static bool EndsWithI(const std::wstring& s, const wchar_t* ext) {
    size_t sl = s.size(), el = wcslen(ext);
    if (sl < el) return false;
    for (size_t i = 0; i < el; i++)
        if (towlower(s[sl - el + i]) != towlower(ext[i])) return false;
    return true;
}

static bool IsPlayableExt(const std::wstring& e) {
    for (const auto& s : SupportedInputExtensions())
        if (e == s) return true;
    return false;
}

static bool IsDirectory(const wchar_t* path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void ExpandDir(const std::wstring& dir, const wchar_t* const* exts, int nExts,
                      std::vector<std::wstring>& out) {
    for (int e = 0; e < nExts; e++) {
        std::wstring pat = dir + L"\\*" + exts[e];
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                out.push_back(dir + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
}

static void ExpandDirPlayable(const std::wstring& dir, std::vector<std::wstring>& out) {
    std::vector<std::wstring> supported = SupportedInputExtensions();
    std::vector<const wchar_t*> exts;
    for (const auto& e : supported) exts.push_back(e.c_str());
    ExpandDir(dir, exts.data(), (int)exts.size(), out);
}

static void PrintInfo(AudioPlayer& player, const wchar_t* filename) {
    const AudioTrackInfo& i = player.Info();
    UI::Set(UI::BOLD_);
    printf("File: ");
    UI::Reset();
    printf("%s\n", WideToUtf8(filename).c_str());

    printf("  Container: %s\n", WideToUtf8(i.formatName).c_str());
    printf("  Format   : %d Hz, %d-bit, %d ch\n", i.sampleRate, i.bits, i.channels);
    if (i.totalBlocks > 0) {
        int secs = i.sampleRate > 0 ? (int)(i.totalBlocks / i.sampleRate) : 0;
        printf("  Length   : %d:%02d (%lld blocks)\n", secs / 60, secs % 60,
               (long long)i.totalBlocks);
    } else {
        printf("  Length   : unknown\n");
    }
    if (i.level != 0) printf("  Level    : %ls\n", CompressionName(i.level));
    if (i.fileVersion != 0) printf("  Version  : %.2f\n", i.fileVersion / 1000.0);
    if (i.avgBitrate > 0)
        printf("  Bitrate  : ~%lld kbps (file %lld bytes)\n", (long long)i.avgBitrate,
               (long long)i.apeBytes);
    else
        printf("  Size     : %lld bytes\n", (long long)i.apeBytes);

    UI::Set(UI::CYAN_);
    printf("Tags:\n");
    UI::Reset();
    PrintTagField(player.TagField(L"Title"), "Title");
    PrintTagField(player.TagField(L"Artist"), "Artist");
    PrintTagField(player.TagField(L"Album"), "Album");
    PrintTagField(player.TagField(L"Genre"), "Genre");
    PrintTagField(player.TagField(L"Year"), "Year");
    PrintTagField(player.TagField(L"Track"), "Track");
    PrintTagField(player.TagField(L"Comment"), "Comment");
    fflush(stdout);
}

static void PrintUsage() {
    UI::Set(UI::BOLD_);
    printf("AudioPlayer v2 - audio player + converter\n");
    UI::Reset();
    printf("Plays and converts WAV, FLAC, APE, MP3, Ogg Vorbis and Opus.\n");
    printf("Usage:\n");
    printf("  AudioPlayer.exe [files/dirs]             Play queue (picker if none)\n");
    printf("  AudioPlayer.exe --info <files...>        Show file info only\n");
    printf("  AudioPlayer.exe --to <fmt> <in...>       Convert audio (any supported in)\n");
    printf("      <fmt>: wav | flac | ape | mp3 | ogg | opus   (default ape)\n");
    printf("      [-c1000|-c2000|-c3000|-c4000|-c5000]  APE compression (default Insane c5000)\n");
    printf("      [--flac-level 0..8] [--quality -0.1..1.0] [--bitrate kbps]  (lossy: kbps)\n");
    printf("      [--wav-bits 16|24] [--threads N] [-t \"Artist=X|Album=Y\"] [--delete-original]\n");
    printf("  AudioPlayer.exe --to-ape <in> [out.ape]  Shorthand for --to ape\n");
    printf("  AudioPlayer.exe --device-list            List audio devices\n");
    printf("  AudioPlayer.exe --help                   This help\n");
    printf("  Flags: --no-color, --ascii\n");
    printf("\nKeys while playing:\n");
    printf("  Space pause/resume, Q/Esc quit, Left/Right seek -/+5s,\n");
    printf("  Up/Down volume, I info, M mute, N next, P previous,\n");
    printf("  L loop mode (off/all/one), S shuffle, ,/. speed -/+0.1x\n");
}

static int ListDevices() {
    ma_context ctx;
    ma_context_config cfg = ma_context_config_init();
    if (ma_context_init(NULL, 0, &cfg, &ctx) != MA_SUCCESS) {
        printf("Failed to init audio context.\n");
        return 3;
    }
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    ma_result r = ma_context_get_devices(&ctx, &infos, &count, nullptr, nullptr);
    if (r != MA_SUCCESS) {
        printf("Failed to list devices.\n");
        ma_context_uninit(&ctx);
        return 3;
    }
    printf("Playback devices (%u):\n", count);
    for (ma_uint32 i = 0; i < count; i++) {
        printf("  [%u] %s%s\n", i, infos[i].name, infos[i].isDefault ? " (default)" : "");
    }
    ma_context_uninit(&ctx);
    return 0;
}

static std::string FmtTime(int64_t blocks, int sampleRate) {
    if (sampleRate <= 0) return "0:00";
    return UI::TimeStr(blocks / sampleRate);
}

// True when the exe owns its console (double-click / drag-and-drop).
// Then we hold the window open before exiting so it never flashes away.
static bool LaunchedFromExplorer() {
    DWORD list[8];
    DWORD n = GetConsoleProcessList(list, 8);
    return n <= 1;
}

static int HoldAndReturn(int code) {
    // No console at all (GUI exe with piped stdio): never hold, never prompt
    // (the prompt would pollute redirected output and _getch has no input).
    if (GetConsoleWindow() == nullptr) return code;
    if (LaunchedFromExplorer()) {
        printf("\nPress any key to exit...");
        fflush(stdout);
        _getch();
        printf("\n");
    }
    return code;
}

// Standard "Open file" dialog. Multi-select supported for queue building.
static bool PickFiles(const wchar_t* filter, std::vector<std::wstring>& out, bool multi) {
    static wchar_t buf[32768];
    buf[0] = 0;
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 32767;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Open - AudioPlayer";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY | (multi ? OFN_ALLOWMULTISELECT : 0);
    if (!GetOpenFileNameW(&ofn)) return false;
    size_t firstLen = wcslen(buf);
    if (buf[firstLen + 1] == 0) {
        out.push_back(buf); // single full path
    } else {
        std::wstring dir = buf;
        const wchar_t* p = buf + firstLen + 1;
        while (*p) {
            out.push_back(dir + L"\\" + p);
            p += wcslen(p) + 1;
        }
    }
    return !out.empty();
}

static void ShuffleOrder(std::vector<int>& order) {
    for (size_t i = order.size(); i > 1; i--) {
        size_t j = (size_t)rand() % i;
        std::swap(order[i - 1], order[j]);
    }
}

enum EndReason { ER_Natural, ER_Next, ER_Prev, ER_Quit };

// Plays one queue entry. Returns how/why it ended.
static EndReason RunTrack(Session& sess, int trackIdx, size_t queuePos, size_t queueSize, int& devErr) {
    devErr = 0;
    const wchar_t* fileArg = sess.tracks[(size_t)trackIdx].c_str();

    AudioPlayer player;
    std::wstring openErr;
    if (!player.Open(fileArg, openErr)) {
        UI::Set(UI::RED_);
        fwprintf(stderr, L"Skipping (%ls): %ls\n", openErr.c_str(), fileArg);
        UI::Reset();
        return ER_Next;
    }
    const AudioTrackInfo& info = player.Info();
    int sampleRate = info.sampleRate;
    int64_t totalBlocks = info.totalBlocks;
    player.SetVolume(sess.volume.load());
    player.SetMuted(sess.muted.load());
    player.SetSpeed(sess.speed.load());

    // Now-playing header.
    std::string disp = !info.title.empty()
        ? WideToUtf8((info.artist.empty() ? info.title : info.artist + L" - " + info.title).c_str())
        : WideToUtf8(BaseName(fileArg).c_str());
    UI::Set(UI::BOLD_);
    UI::Set(UI::GREEN_);
    printf("\n\xE2\x99\xAA Now playing [%u/%u]: %s\n", (unsigned)(queuePos + 1),
           (unsigned)queueSize, disp.c_str());
    UI::Reset();
    UI::Set(UI::GRAY_);
    printf("  %ls %d Hz %d-bit %dch  %s\n", info.formatName.c_str(), sampleRate, info.bits,
           info.channels, WideToUtf8(BaseName(fileArg).c_str()).c_str());
    UI::Reset();
    fflush(stdout);

    std::wstring devErrText;
    if (!player.Start(devErrText)) {
        UI::Set(UI::RED_);
        fwprintf(stderr, L"Failed to start audio device (%ls).\n", devErrText.c_str());
        UI::Reset();
        devErr = 3;
        return ER_Quit;
    }

    EndReason reason = ER_Natural;
    bool quit = false;
    while (!quit) {
        if (player.Finished()) {
            UI::EndProgress();
            UI::Set(UI::GREEN_);
            printf("Done.\n");
            UI::Reset();
            reason = ER_Natural;
            break;
        }
        int64_t played = player.Position();
        double frac = totalBlocks > 0 ? (double)played / (double)totalBlocks : 0;
        float spd = player.Speed();
        char spdTxt[32] = "";
        if (spd != 1.0f) {
            if (spd < 1000.0f) snprintf(spdTxt, sizeof(spdTxt), "   %.2fx", (double)spd);
            else snprintf(spdTxt, sizeof(spdTxt), "   %.4gx", (double)spd);
        }
        char detail[224];
        if (totalBlocks > 0) {
            snprintf(detail, sizeof(detail), "%s / %s   Vol %d%%%s%s   %s%s%s",
                     FmtTime(played, sampleRate).c_str(), FmtTime(totalBlocks, sampleRate).c_str(),
                     (int)(sess.volume.load() * 100 + 0.5),
                     sess.muted.load() ? " MUTED" : "", player.Paused() ? " PAUSED" : "",
                     UI::LoopName(sess.loop), sess.shuffle ? " SHUFFLE" : "", spdTxt);
        } else {
            snprintf(detail, sizeof(detail), "%s   Vol %d%%%s%s   %s%s%s",
                     FmtTime(played, sampleRate).c_str(),
                     (int)(sess.volume.load() * 100 + 0.5),
                     sess.muted.load() ? " MUTED" : "", player.Paused() ? " PAUSED" : "",
                     UI::LoopName(sess.loop), sess.shuffle ? " SHUFFLE" : "", spdTxt);
        }
        if (player.Paused()) UI::Set(UI::YELLOW_);
        UI::Progress("Playing", frac, detail);
        UI::Reset();

        if (_kbhit()) {
            int c = _getch();
            if (c == 0 || c == 224) {
                int c2 = _getch();
                if (c2 == 75) { // left
                    int64_t target = played - (int64_t)sampleRate * 5;
                    if (target < 0) target = 0;
                    player.SeekTo(target);
                } else if (c2 == 77) { // right
                    int64_t target = played + (int64_t)sampleRate * 5;
                    if (totalBlocks > 0 && target >= totalBlocks) target = totalBlocks - 1;
                    player.SeekTo(target);
                } else if (c2 == 72) { // up vol+
                    float v = sess.volume.load() + 0.1f;
                    if (v > 1.5f) v = 1.5f;
                    sess.volume = v;
                    player.SetVolume(v);
                    sess.muted = false;
                    player.SetMuted(false);
                } else if (c2 == 80) { // down vol-
                    float v = sess.volume.load() - 0.1f;
                    if (v < 0.0f) v = 0.0f;
                    sess.volume = v;
                    player.SetVolume(v);
                }
            } else if (c == ' ') {
                player.SetPaused(!player.Paused());
            } else if (c == 'q' || c == 'Q' || c == 27) {
                reason = ER_Quit; quit = true;
            } else if (c == 'n' || c == 'N') {
                reason = ER_Next; quit = true;
            } else if (c == 'p' || c == 'P') {
                // Standard: restart if well into the track, else previous.
                if (played > (int64_t)sampleRate * 3) {
                    player.SeekTo(0);
                } else {
                    reason = ER_Prev; quit = true;
                }
            } else if (c == 'l' || c == 'L') {
                sess.loop = (sess.loop + 1) % 3;
            } else if (c == 's' || c == 'S') {
                sess.shuffle = !sess.shuffle;
                if (sess.shuffle) {
                    ShuffleOrder(sess.order);
                    for (size_t i = 0; i < sess.order.size(); i++)
                        if (sess.order[i] == trackIdx) { sess.pos = i; break; }
                } else {
                    sess.order.clear();
                    for (int i = 0; i < (int)sess.tracks.size(); i++) sess.order.push_back(i);
                    sess.pos = (size_t)trackIdx;
                }
            } else if (c == 'i' || c == 'I') {
                UI::EndProgress();
                printf("\n");
                PrintInfo(player, fileArg);
            } else if (c == 'm' || c == 'M') {
                bool m = !sess.muted.load();
                sess.muted = m;
                player.SetMuted(m);
            } else if (c == '+' || c == '=') {
                float v = sess.volume.load() + 0.1f;
                if (v > 1.5f) v = 1.5f;
                sess.volume = v;
                player.SetVolume(v);
                sess.muted = false;
                player.SetMuted(false);
            } else if (c == '-' || c == '_') {
                float v = sess.volume.load() - 0.1f;
                if (v < 0.0f) v = 0.0f;
                sess.volume = v;
                player.SetVolume(v);
            } else if (c == ',' || c == '.') {
                float v = sess.speed.load() + (c == '.' ? 0.1f : -0.1f);
                if (!(v > 0.0f)) v = 0.01f; // never drive at/below zero
                sess.speed = v;
                player.SetSpeed(v);
            }
        }
        Sleep(50);
    }

    UI::EndProgress();
    return reason;
}

static int RunEncode(const std::vector<std::wstring>& positionals, const EncodeOptions& base) {
    // Expand dirs -> supported inputs; keep files as given.
    std::vector<std::wstring> supported = SupportedInputExtensions();
    std::vector<const wchar_t*> exts;
    for (const auto& e : supported) exts.push_back(e.c_str());
    std::vector<std::wstring> inputs;
    for (const auto& p : positionals) {
        if (IsDirectory(p.c_str())) {
            size_t before = inputs.size();
            ExpandDir(p, exts.data(), (int)exts.size(), inputs);
            if (inputs.size() == before)
                fwprintf(stderr, L"No convertible audio in folder: %ls\n", p.c_str());
        } else {
            std::wstring e = FileExtension(p);
            bool ok = false;
            for (const auto& s : supported)
                if (e == s) { ok = true; break; }
            if (ok) inputs.push_back(p);
            else fwprintf(stderr, L"Skipping (not convertible): %ls\n", p.c_str());
        }
    }
    if (inputs.empty()) {
        fwprintf(stderr, L"No convertible input found.\n");
        return 1;
    }

    UI::Banner();
    int ok = 0, fail = 0, rc = 0;
    for (size_t i = 0; i < inputs.size(); i++) {
        std::wstring out = AutoOutName(inputs[i], base.format);
        if (inputs.size() > 1)
            printf("\n[%u/%u]\n", (unsigned)(i + 1), (unsigned)inputs.size());
        else
            printf("\n");
        EncodeOptions opt = base;
        int r = EncodeToFile(inputs[i], out, opt);
        if (r == 0) ok++;
        else { fail++; rc = r; }
    }
    printf("\n");
    if (fail == 0) {
        UI::Set(UI::GREEN_);
        printf("All %d file(s) converted.\n", ok);
    } else {
        UI::Set(UI::YELLOW_);
        printf("%d converted, %d failed.\n", ok, fail);
    }
    UI::Reset();
    return rc;
}

// Console entry used by the GUI exe when CLI verbs are given
// (attaches to the parent console first). See win_gui.cpp.
int ConsoleMain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    srand((unsigned)time(nullptr));

    bool wantInfo = false, wantDevices = false, wantHelp = false, wantEncode = false;
    bool noColor = false, ascii = false, deleteOriginal = false;
    int level = 5000, threads = 0;
    OutFormat outFmt = OutFormat::APE;
    int flacLevel = 5;
    float vorbisQ = 0.4f;
    int bitrateKbps = 0; // 0 = per-format default (Opus 96, MP3 192)
    int wavBits = 16;
    std::wstring tagOverride;
    std::vector<std::wstring> positionals;

    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--help" || a == L"-h" || a == L"/?") wantHelp = true;
        else if (a == L"--info") wantInfo = true;
        else if (a == L"--device-list") wantDevices = true;
        else if (a == L"--to-ape") wantEncode = true;
        else if (a == L"--to") {
            if (i + 1 >= argc) { fwprintf(stderr, L"--to needs a format: wav|flac|ape|mp3|ogg|opus.\n"); return HoldAndReturn(1); }
            if (!ParseOutFormat(argv[++i], outFmt)) { fwprintf(stderr, L"Unknown format (want wav|flac|ape|mp3|ogg|opus).\n"); return HoldAndReturn(1); }
            wantEncode = true;
        } else if (a == L"--flac-level") {
            if (i + 1 >= argc) { fwprintf(stderr, L"--flac-level needs 0..8.\n"); return HoldAndReturn(1); }
            flacLevel = _wtoi(argv[++i]);
            if (flacLevel < 0 || flacLevel > 8) { fwprintf(stderr, L"Bad --flac-level (want 0..8).\n"); return HoldAndReturn(1); }
        } else if (a == L"--quality") {
            if (i + 1 >= argc) { fwprintf(stderr, L"--quality needs -0.1..1.0.\n"); return HoldAndReturn(1); }
            vorbisQ = (float)_wtof(argv[++i]);
            if (vorbisQ < -0.1f || vorbisQ > 1.0f) { fwprintf(stderr, L"Bad --quality (want -0.1..1.0).\n"); return HoldAndReturn(1); }
        } else if (a == L"--bitrate") {
            if (i + 1 >= argc) { fwprintf(stderr, L"--bitrate needs kbps.\n"); return HoldAndReturn(1); }
            bitrateKbps = _wtoi(argv[++i]);
            if (bitrateKbps <= 0 || bitrateKbps > 510) { fwprintf(stderr, L"Bad --bitrate.\n"); return HoldAndReturn(1); }
        } else if (a == L"--wav-bits") {
            if (i + 1 >= argc) { fwprintf(stderr, L"--wav-bits needs 16 or 24.\n"); return HoldAndReturn(1); }
            wavBits = _wtoi(argv[++i]);
            if (wavBits != 16 && wavBits != 24) { fwprintf(stderr, L"Bad --wav-bits (want 16 or 24).\n"); return HoldAndReturn(1); }
        } else if (a == L"--no-color") noColor = true;
        else if (a == L"--ascii") ascii = true;
        else if (a == L"--delete-original") deleteOriginal = true;
        else if (a == L"-t") {
            if (i + 1 >= argc) { fwprintf(stderr, L"-t needs \"Name=Value|...\" after it.\n"); return HoldAndReturn(1); }
            tagOverride = argv[++i];
        } else if (a.compare(0, 10, L"--threads=") == 0) {
            threads = _wtoi(a.substr(10).c_str());
        } else if (a == L"--threads") {
            if (i + 1 >= argc) { fwprintf(stderr, L"--threads needs a number.\n"); return HoldAndReturn(1); }
            threads = _wtoi(argv[++i]);
        } else if (a.size() > 2 && a[0] == L'-' && a[1] == L'c') {
            level = _wtoi(a.substr(2).c_str());
            if (level != 1000 && level != 2000 && level != 3000 && level != 4000 && level != 5000) {
                fwprintf(stderr, L"Bad level '%ls' (want -c1000..-c5000).\n", a.c_str());
                return HoldAndReturn(1);
            }
        } else if (!a.empty() && a[0] == L'-') {
            fwprintf(stderr, L"Unknown option: %ls\n", a.c_str());
            PrintUsage();
            return HoldAndReturn(1);
        } else {
            positionals.push_back(a);
        }
    }

    UI::Init(noColor, ascii);

    if (wantHelp) { PrintUsage(); return HoldAndReturn(0); }
    if (wantDevices) return HoldAndReturn(ListDevices());

    if (wantEncode) {
        // --to fmt / --to-ape [in] [out] | [many...]: if exactly 2 positionals
        // and the second has an audio output extension, treat it as the name.
        std::wstring explicitOut;
        std::vector<std::wstring> inputs = positionals;
        if (inputs.empty()) {
            std::vector<std::wstring> picked;
            if (!PickFiles(L"Audio (*.wav,*.flac,*.ape,*.mp3,*.ogg,*.opus)\0*.wav;*.flac;*.ape;*.mp3;*.ogg;*.opus\0All files (*.*)\0*.*\0", picked, true)) {
                PrintUsage();
                return HoldAndReturn(1);
            }
            inputs = picked;
        }
        if (inputs.size() == 2 && !IsDirectory(inputs[1].c_str())) {
            std::wstring e2 = FileExtension(inputs[1]);
            for (int f = 0; f < 6; f++) {
                if (e2 == OutFormatExtension((OutFormat)f)) {
                    explicitOut = inputs[1];
                    inputs.erase(inputs.begin() + 1);
                    break;
                }
            }
        }
        EncodeOptions base;
        base.format = outFmt;
        base.level = level; base.threads = threads; base.tagOverride = tagOverride;
        base.deleteOriginal = deleteOriginal;
        base.flacLevel = flacLevel; base.vorbisQuality = vorbisQ;
        base.opusBitrate = bitrateKbps > 0 ? bitrateKbps * 1000 : 96000;
        base.mp3Bitrate = bitrateKbps > 0 ? bitrateKbps : 192;
        base.wavBits = wavBits;
        int rc;
        if (!explicitOut.empty()) {
            // Single input -> explicit output (dirs already excluded above).
            if (IsDirectory(inputs[0].c_str())) {
                fwprintf(stderr, L"An explicit output needs a single input file.\n");
                return HoldAndReturn(1);
            }
            UI::Banner();
            printf("\n");
            rc = EncodeToFile(inputs[0], explicitOut, base);
        } else {
            // NOTE: RunEncode re-expands dirs itself, so pass raw positionals.
            rc = RunEncode(inputs, base);
        }
        return HoldAndReturn(rc);
    }

    if (wantInfo) {
        if (positionals.empty()) { PrintUsage(); return HoldAndReturn(1); }
        std::vector<std::wstring> supported = SupportedInputExtensions();
        std::vector<const wchar_t*> exts;
        for (const auto& e : supported) exts.push_back(e.c_str());
        std::vector<std::wstring> files;
        for (const auto& p : positionals) {
            if (IsDirectory(p.c_str())) ExpandDir(p, exts.data(), (int)exts.size(), files);
            else files.push_back(p);
        }
        int rc = 0;
        UI::Banner();
        for (const auto& f : files) {
            AudioPlayer player;
            std::wstring err;
            if (!player.Open(f.c_str(), err)) {
                UI::Set(UI::RED_);
                fwprintf(stderr, L"Cannot open (%ls): %ls\n", err.c_str(), f.c_str());
                UI::Reset();
                rc = 2;
                continue;
            }
            printf("\n");
            PrintInfo(player, f.c_str());
        }
        return HoldAndReturn(rc);
    }

    // ---- play mode ----
    std::vector<std::wstring> supported = SupportedInputExtensions();
    std::vector<const wchar_t*> playExts;
    for (const auto& e : supported) playExts.push_back(e.c_str());
    std::vector<std::wstring> queue;
    if (positionals.empty()) {
        std::vector<std::wstring> picked;
        if (!PickFiles(L"Audio (*.wav,*.flac,*.ape,*.mp3,*.ogg,*.opus)\0*.wav;*.flac;*.ape;*.mp3;*.ogg;*.opus\0All files (*.*)\0*.*\0", picked, true)) {
            PrintUsage();
            return HoldAndReturn(1);
        }
        positionals = picked;
    }
    for (const auto& p : positionals) {
        if (IsDirectory(p.c_str())) {
            ExpandDir(p, playExts.data(), (int)playExts.size(), queue);
        } else if (IsPlayableExt(FileExtension(p))) {
            queue.push_back(p);
        } else {
            fwprintf(stderr, L"Skipping (not playable): %ls\n", p.c_str());
        }
    }
    if (queue.empty()) {
        fwprintf(stderr, L"No playable files in the queue.\n");
        PrintUsage();
        return HoldAndReturn(1);
    }

    UI::Banner();
    Session sess;
    sess.tracks = queue;
    for (int i = 0; i < (int)queue.size(); i++) sess.order.push_back(i);

    while (sess.pos < sess.order.size()) {
        int ti = sess.order[sess.pos];
        int devErr = 0;
        EndReason r = RunTrack(sess, ti, sess.pos, sess.order.size(), devErr);
        if (devErr) return HoldAndReturn(devErr);
        if (r == ER_Quit) break;
        if (r == ER_Prev) {
            if (sess.pos > 0) sess.pos--;
            continue;
        }
        if (r == ER_Natural && sess.loop == 2) continue; // repeat one
        sess.pos++;
        if (sess.pos >= sess.order.size() && sess.loop == 1) sess.pos = 0; // repeat all
    }
    return HoldAndReturn(0);
}
