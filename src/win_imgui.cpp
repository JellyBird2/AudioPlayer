// win_imgui.cpp - Dear ImGui frontend (Win32 + DirectX11).
// AudioPlayer for decode/playback, encode.h for any-to-any conversion,
// win_art.h for cover-art decoding.
// Playlist, transport, cover art, convert dialog, track details.
// CLI verbs (--to-ape/--info/...) attach to the parent console and run the
// console UI instead (see cli.cpp).
#include "win_player.h"
#include "win_art.h"
#include "encode.h"
#include "win_ids.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <windows.h>
#include <objbase.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wctype.h>
#include <io.h>
#include <fcntl.h>

#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>

extern int ConsoleMain(int argc, wchar_t** argv);

// Forward declare message handler from imgui_impl_win32.cpp.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------- DX11 state
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();

// ---------------------------------------------------------------- app state
struct GuiTrack {
    std::wstring path;
    std::wstring title;
    std::wstring artist;
    std::string dispUtf8; // cached "Artist - Title" or basename
    int secs = 0;
};

static AudioPlayer g_player;
static std::vector<GuiTrack> g_tracks;
static std::vector<int> g_order;
static size_t g_pos = 0;
static int g_loop = 0; // 0 off, 1 all, 2 one
static bool g_shuffle = false;
static bool g_hasCurrent = false;
static float g_volume = 1.0f;
static bool g_muted = false;
static bool g_preservePitch = true;
static int g_seekPos = 0; // 0..1000 slider mirror
static char g_statusState[64] = "Ready";
static int g_selectedRow = -1;
static bool g_scrollToCurrent = false;

static ID3D11ShaderResourceView* g_coverSRV = nullptr;
static int g_coverW = 0, g_coverH = 0;
// Latest extra tag strings, cached on track load (avoids per-frame lookups).
static std::string g_detailGenre, g_detailYear, g_detailTrack;
// Async cover-art handoff: worker decodes HBITMAP, UI thread uploads the SRV.
static std::mutex g_coverMtx;
static std::atomic<unsigned> g_coverGen{0};
static std::atomic<bool> g_coverShutdown{false};
static HBITMAP g_pendingBmp = nullptr; // guarded by g_coverMtx
static unsigned g_pendingGen = 0;      // guarded by g_coverMtx
static bool g_pendingHave = false;     // guarded by g_coverMtx

static HWND g_hwnd = nullptr;
static std::vector<std::wstring> g_pendingDrops;

static bool g_showConvert = false;
static bool g_showDetails = false;
static bool g_showAbout = false;
static bool g_lightTheme = false;
static bool g_followSystemTheme = true; // default: match Windows app theme
static float g_dpiScale = 1.0f;
static std::string g_detailsText;

// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on Win10 1809+; older MinGW headers
// may not define it, so fall back to the numeric value.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void ApplyTheme();
static bool SystemWantsLightTheme();
static void SyncThemeWithSystem();

// Convert worker state (touched from worker thread via atomics/mutex only).
struct ConvState {
    std::vector<std::wstring> jobs; // input paths (output derived from format)
    int formatIdx = 2;              // into kConvFormats (0=WAV 1=FLAC 2=APE 3=MP3 4=Vorbis 5=Opus)
    int level = 5000;
    int flacLevel = 5;
    float vorbisQ = 0.4f;
    int bitrateKbps = 0; // 0 = default (MP3 192, Opus 96)
    int wavBits = 16;    // 16 or 24 (WAV output)
    int threads = 0;
    std::string tagsUtf8;
    char tagsBuf[1024] = "";
    bool delOrig = false;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<int> curIdx{0};
    std::atomic<float> frac{0.0f};
    std::mutex detailMtx;
    std::string detail;
    std::vector<int> results;
    std::string summary;
    std::vector<std::string> dispNames; // cached output basenames for dispFmt
    int dispFmt = -1;
};
static ConvState g_conv;

// Convert-modal format list, least → most compressed. Order must match
// ConvState::formatIdx mapping in OpenConvertFor/ConvWorkerMain/modal.
static const OutFormat kConvFormats[] = {OutFormat::WAV, OutFormat::FLAC, OutFormat::APE,
                                         OutFormat::MP3, OutFormat::VORBIS, OutFormat::OPUS};
static const char* kConvFormatNames[] = {"WAV (uncompressed)", "FLAC (lossless)", "APE (lossless)",
                                         "MP3 (lossy)", "Ogg Vorbis (lossy)", "Opus (lossy)"};

struct GuiSink : public IEncodeProgress {
    explicit GuiSink(ConvState* s) : st(s) {}
    void OnProgress(double f, const char* d) override {
        st->frac = (float)f;
        std::lock_guard<std::mutex> lk(st->detailMtx);
        st->detail = d ? d : "";
    }
    bool Cancelled() override { return st->cancel.load(); }
    ConvState* st;
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

static std::wstring BaseName(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/:");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

static bool EndsWithI(const std::wstring& s, const wchar_t* ext) {
    size_t sl = s.size(), el = wcslen(ext);
    if (sl < el) return false;
    for (size_t i = 0; i < el; i++)
        if (towlower(s[sl - el + i]) != towlower(ext[i])) return false;
    return true;
}

static bool IsDir(const wchar_t* p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string DisplayUtf8(const GuiTrack& t) {
    if (!t.artist.empty() && !t.title.empty())
        return WideToUtf8(t.artist + L" - " + t.title);
    if (!t.title.empty()) return WideToUtf8(t.title);
    return WideToUtf8(BaseName(t.path));
}

static void ShuffleOrder(std::vector<int>& order) {
    for (size_t i = order.size(); i > 1; i--) {
        size_t j = (size_t)rand() % i;
        std::swap(order[i - 1], order[j]);
    }
}

static bool ReadMeta(const std::wstring& path, GuiTrack& out) {
    AudioPlayer tmp;
    std::wstring err;
    if (!tmp.Open(path.c_str(), err)) return false;
    out.path = path;
    out.title = tmp.Info().title;
    out.artist = tmp.Info().artist;
    int rate = tmp.Info().sampleRate;
    out.secs = rate > 0 ? (int)(tmp.Info().totalBlocks / rate) : 0;
    out.dispUtf8 = DisplayUtf8(out);
    return true;
}

// HBITMAP (32bpp, as produced by DecodeCoverArt) -> DX11 shader resource view.
static bool CreateSRVFromHBITMAP(ID3D11Device* dev, HBITMAP hbmp,
                                 ID3D11ShaderResourceView** outSRV, int* outW, int* outH) {
    if (!dev || !hbmp || !outSRV) return false;
    BITMAP bm = {};
    if (!GetObjectW(hbmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return false;
    const int w = bm.bmWidth, h = bm.bmHeight;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> px((size_t)w * (size_t)h * 4);
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, hbmp, 0, (UINT)h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != h) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = px.data();
    init.SysMemPitch = (UINT)(w * 4);
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&desc, &init, &tex))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = desc.Format;
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    vd.Texture2D.MipLevels = 1;
    HRESULT hr = dev->CreateShaderResourceView(tex, &vd, outSRV);
    tex->Release();
    if (FAILED(hr)) return false;
    if (outW) *outW = w;
    if (outH) *outH = h;
    return true;
}

static void FreeCover() {
    ++g_coverGen; // invalidate any in-flight decode
    if (g_coverSRV) {
        g_coverSRV->Release();
        g_coverSRV = nullptr;
    }
    g_coverW = g_coverH = 0;
    std::lock_guard<std::mutex> lk(g_coverMtx);
    if (g_pendingHave && g_pendingBmp) DeleteObject(g_pendingBmp);
    g_pendingBmp = nullptr;
    g_pendingHave = false;
}

static void CoverWorker(unsigned gen, std::vector<unsigned char> img) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    HBITMAP hbmp = DecodeCoverArt(img.data(), img.size(), 600);
    bool adopted = false;
    if (hbmp && !g_coverShutdown.load()) {
        std::lock_guard<std::mutex> lk(g_coverMtx);
        if (gen == g_coverGen.load() && !g_coverShutdown.load()) {
            if (g_pendingHave && g_pendingBmp) DeleteObject(g_pendingBmp);
            g_pendingBmp = hbmp;
            g_pendingGen = gen;
            g_pendingHave = true;
            adopted = true;
        }
    }
    if (!adopted && hbmp) DeleteObject(hbmp);
    CoUninitialize();
}

// Releases the old art and kicks off a background decode for the current
// track. Returns immediately; PollCover() picks up the result.
static void LoadCover() {
    FreeCover();
    if (!g_hasCurrent || !g_pd3dDevice || g_coverShutdown.load()) return;
    std::vector<unsigned char> img = g_player.CoverImage();
    if (img.empty()) return;
    std::thread(CoverWorker, g_coverGen.load(), std::move(img)).detach();
}

// UI thread: adopt a finished decode (if still current) and upload the SRV.
static void PollCover() {
    HBITMAP hbmp = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_coverMtx);
        if (!g_pendingHave) return;
        if (g_pendingGen != g_coverGen.load()) { // superseded
            if (g_pendingBmp) DeleteObject(g_pendingBmp);
            g_pendingBmp = nullptr;
            g_pendingHave = false;
            return;
        }
        hbmp = g_pendingBmp;
        g_pendingBmp = nullptr;
        g_pendingHave = false;
    }
    if (hbmp) {
        if (g_coverSRV) {
            g_coverSRV->Release();
            g_coverSRV = nullptr;
        }
        CreateSRVFromHBITMAP(g_pd3dDevice, hbmp, &g_coverSRV, &g_coverW, &g_coverH);
        DeleteObject(hbmp);
    }
}

// ---------------------------------------------------------------- playback
static void SetState(const char* s) {
    snprintf(g_statusState, sizeof(g_statusState), "%s", s);
}

static bool PlayAt(size_t pos) {
    if (pos >= g_order.size()) return false;
    g_pos = pos;
    int ti = g_order[pos];
    g_player.Stop();
    FreeCover();
    g_detailGenre.clear();
    g_detailYear.clear();
    g_detailTrack.clear();
    std::wstring err;
    if (!g_player.Open(g_tracks[(size_t)ti].path.c_str(), err)) {
        g_hasCurrent = false;
        SetState("Error");
        return true;
    }
    if (!g_player.Start(err)) {
        g_player.Close();
        g_hasCurrent = false;
        SetState("Error");
        return true;
    }
    g_player.SetVolume(g_volume);
    g_player.SetMuted(g_muted);
    g_hasCurrent = true;
    g_detailGenre = WideToUtf8(g_player.TagField(L"Genre"));
    g_detailYear = WideToUtf8(g_player.TagField(L"Year"));
    g_detailTrack = WideToUtf8(g_player.TagField(L"Track"));
    g_seekPos = 0;
    g_selectedRow = (int)pos;
    g_scrollToCurrent = true;
    LoadCover();
    SetState("Playing");
    return true;
}

static void StopPlayback() {
    if (!g_hasCurrent) {
        SetState("Ready");
        return;
    }
    g_player.SeekTo(0);
    g_player.Stop();
    g_seekPos = 0;
    SetState("Stopped");
}

static void DoPlayPause() {
    if (!g_hasCurrent) {
        if (!g_order.empty()) PlayAt(g_pos);
        return;
    }
    if (g_player.DeviceRunning() && !g_player.Paused()) {
        g_player.SetPaused(true);
        SetState("Paused");
    } else if (g_player.Paused()) {
        g_player.SetPaused(false);
        SetState("Playing");
    } else {
        std::wstring err;
        if (g_player.Start(err))
            SetState("Playing");
        else
            SetState("Error");
    }
}

static void AdvanceAfterEnd() {
    if (g_loop == 2) {
        g_player.Replay();
        SetState("Playing");
        return;
    }
    size_t next = g_pos + 1;
    if (next >= g_order.size()) {
        if (g_loop == 1) {
            next = 0;
        } else {
            g_player.Stop();
            g_seekPos = 1000;
            SetState("Stopped");
            return;
        }
    }
    PlayAt(next);
}

static void RemoveRow(int sel) {
    if (sel < 0 || (size_t)sel >= g_order.size()) return;
    int removedTrack = g_order[(size_t)sel];
    bool wasCurrent = g_hasCurrent && (size_t)sel == g_pos;
    if (wasCurrent) {
        g_player.Stop();
        g_player.Close();
        g_hasCurrent = false;
        FreeCover();
    }
    g_tracks.erase(g_tracks.begin() + removedTrack);
    for (auto& t : g_tracks) t.dispUtf8 = DisplayUtf8(t);
    std::vector<int> no;
    for (int v : g_order) {
        if (v == removedTrack) continue;
        no.push_back(v > removedTrack ? v - 1 : v);
    }
    g_order = no;
    if (g_pos >= g_order.size()) g_pos = g_order.empty() ? 0 : g_order.size() - 1;
    if (g_selectedRow >= (int)g_order.size()) g_selectedRow = (int)g_order.size() - 1;
    if (wasCurrent && !g_order.empty())
        PlayAt(g_pos);
    else if (!g_hasCurrent)
        SetState("Ready");
}

static void AddPaths(const std::vector<std::wstring>& paths, bool& addedAny) {
    addedAny = false;
    g_tracks.reserve(g_tracks.size() + paths.size());
    std::vector<std::wstring> playable = SupportedInputExtensions();
    auto playableExt = [&](const std::wstring& f) {
        size_t dot = f.find_last_of(L'.');
        size_t sep = f.find_last_of(L"\\/:");
        if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return false;
        std::wstring e = f.substr(dot);
        for (auto& c : e) c = (wchar_t)towlower(c);
        for (const auto& s : playable)
            if (e == s) return true;
        return false;
    };
    for (const auto& p : paths) {
        if (IsDir(p.c_str())) {
            std::vector<std::wstring> found;
            for (const auto& e : playable) {
                std::wstring pat = p + L"\\*" + e;
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW(pat.c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        found.push_back(p + L"\\" + fd.cFileName);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            std::sort(found.begin(), found.end(), [](const std::wstring& a, const std::wstring& b) {
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
            for (const auto& f : found) {
                GuiTrack t;
                if (ReadMeta(f, t)) {
                    g_tracks.push_back(t);
                    addedAny = true;
                }
            }
        } else if (playableExt(p)) {
            GuiTrack t;
            if (ReadMeta(p, t)) {
                g_tracks.push_back(t);
                addedAny = true;
            }
        }
    }
    int curTrack = (g_hasCurrent && g_pos < g_order.size()) ? g_order[g_pos] : -1;
    g_order.clear();
    for (int i = 0; i < (int)g_tracks.size(); i++) g_order.push_back(i);
    if (g_shuffle) ShuffleOrder(g_order);
    g_pos = 0;
    if (curTrack >= 0) {
        for (size_t i = 0; i < g_order.size(); i++)
            if (g_order[i] == curTrack) {
                g_pos = i;
                break;
            }
    }
    if (g_selectedRow >= (int)g_order.size()) g_selectedRow = -1;
}

// ---------------------------------------------------------------- dialogs
// "Audio (*.wav,*...)\0*.wav;...\0All files (*.*)\0*.*\0" for the open
// dialog, built from the supported playback extensions (embedded NULs).
static const wchar_t* AudioFileFilter() {
    static std::wstring f;
    static bool init = false;
    if (!init) {
        init = true;
        std::wstring desc, pat;
        bool first = true;
        for (const auto& e : SupportedInputExtensions()) {
            if (!first) {
                desc += L",";
                pat += L";";
            }
            first = false;
            desc += L"*" + e;
            pat += L"*" + e;
        }
        f = L"Audio (";
        f += desc;
        f += L")";
        f.push_back(0);
        f += pat;
        f.push_back(0);
        f += L"All files (*.*)";
        f.push_back(0);
        f += L"*.*";
        f.push_back(0);
        f.push_back(0);
    }
    return f.c_str();
}

static bool OpenFilesDialog(std::vector<std::wstring>& out, const wchar_t* filter,
                            const wchar_t* title) {
    static wchar_t buf[32768];
    buf[0] = 0;
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 32767;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return false;
    size_t firstLen = wcslen(buf);
    if (buf[firstLen + 1] == 0) {
        out.push_back(buf);
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

static bool BrowseFolder(std::wstring& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Pick a folder (all audio files inside will be queued) - AudioPlayer");
    bool ok = false;
    if (SUCCEEDED(dlg->Show(g_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            wchar_t* p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out = p;
                CoTaskMemFree(p);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

static std::string BuildDetailsText(const wchar_t* path) {
    AudioPlayer p;
    std::wstring err;
    std::string t = "File: " + WideToUtf8(path) + "\n";
    char line[512];
    if (!p.Open(path, err)) {
        t += "Cannot open: " + WideToUtf8(err) + "\n";
        return t;
    }
    const AudioTrackInfo& i = p.Info();
    t += "Container: " + WideToUtf8(i.formatName) + "\n";
    snprintf(line, sizeof(line), "Format: %d Hz, %d-bit, %d ch\n", i.sampleRate, i.bits, i.channels);
    t += line;
    if (i.totalBlocks > 0) {
        int secs = i.sampleRate > 0 ? (int)(i.totalBlocks / i.sampleRate) : 0;
        snprintf(line, sizeof(line), "Length: %d:%02d (%lld blocks)\n", secs / 60, secs % 60,
                 (long long)i.totalBlocks);
    } else {
        snprintf(line, sizeof(line), "Length: unknown\n");
    }
    t += line;
    if (i.level != 0) t += "Level: " + WideToUtf8(CompressionName(i.level)) + "\n";
    if (i.fileVersion != 0) {
        snprintf(line, sizeof(line), "Version: %.2f\n", i.fileVersion / 1000.0);
        t += line;
    }
    if (i.avgBitrate > 0)
        snprintf(line, sizeof(line), "Bitrate: ~%lld kbps (%lld bytes)\n", (long long)i.avgBitrate,
                 (long long)i.apeBytes);
    else
        snprintf(line, sizeof(line), "Size: %lld bytes\n", (long long)i.apeBytes);
    t += line;
    t += "Tags:\n";
    const wchar_t* names[] = {L"Title", L"Artist", L"Album", L"Genre", L"Year", L"Track", L"Comment"};
    bool any = false;
    for (const wchar_t* n : names) {
        std::wstring v = p.TagField(n);
        if (!v.empty()) {
            t += "  " + WideToUtf8(n) + ": " + WideToUtf8(v) + "\n";
            any = true;
        }
    }
    if (!any) t += "  (none)\n";
    return t;
}

// ---------------------------------------------------------------- convert worker
static void ConvWorkerMain(ConvState* st) {
    GuiSink sink(st);
    for (size_t i = 0; i < st->jobs.size(); i++) {
        if (st->cancel.load()) break;
        st->curIdx = (int)i;
        st->frac = 0.0f;
        {
            std::lock_guard<std::mutex> lk(st->detailMtx);
            st->detail.clear();
        }
        EncodeOptions opt;
        opt.format = kConvFormats[st->formatIdx];
        opt.level = st->level;
        opt.flacLevel = st->flacLevel;
        opt.vorbisQuality = st->vorbisQ;
        opt.opusBitrate = st->bitrateKbps > 0 ? st->bitrateKbps * 1000 : 96000;
        opt.mp3Bitrate = st->bitrateKbps > 0 ? st->bitrateKbps : 192;
        opt.wavBits = (st->wavBits == 24) ? 24 : 16;
        opt.threads = st->threads;
        if (!st->tagsUtf8.empty()) {
            int n = MultiByteToWideChar(CP_UTF8, 0, st->tagsUtf8.c_str(), -1, nullptr, 0);
            if (n > 0) {
                std::wstring w((size_t)(n - 1), 0);
                MultiByteToWideChar(CP_UTF8, 0, st->tagsUtf8.c_str(), -1, w.data(), n);
                opt.tagOverride = w;
            }
        }
        opt.deleteOriginal = st->delOrig;
        int rc = EncodeToFile(st->jobs[i], AutoOutName(st->jobs[i], opt.format), opt, &sink);
        st->results.push_back(rc);
        if (rc == 5) break; // cancelled
    }
    int ok = 0, fail = 0, cancelled = 0;
    for (int r : st->results) {
        if (r == 0) ok++;
        else if (r == 5) cancelled++;
        else fail++;
    }
    if (st->cancel.load() && (size_t)(ok + fail + cancelled) < st->jobs.size())
        cancelled += (int)(st->jobs.size() - (ok + fail + cancelled));
    char b[256];
    if (cancelled > 0 && ok == 0 && fail == 0)
        snprintf(b, sizeof(b), "Cancelled.");
    else
        snprintf(b, sizeof(b), "Finished: %d converted, %d failed%s.", ok, fail,
                 cancelled ? " (cancelled)" : "");
    st->summary = b;
    st->done = true;
    st->running = false;
}

static void ConvStart(ConvState* st) {
    if (st->running.load() || st->jobs.empty()) return;
    st->tagsUtf8 = st->tagsBuf;
    st->cancel = false;
    st->done = false;
    st->curIdx = 0;
    st->frac = 0.0f;
    st->results.clear();
    st->summary.clear();
    st->running = true;
    st->worker = std::thread(ConvWorkerMain, st);
}

static void ConvCancel(ConvState* st) {
    st->cancel = true;
}

static void ConvJoin(ConvState* st) {
    if (st->worker.joinable()) st->worker.join();
}

static void OpenConvertFor(const std::vector<std::wstring>& picked) {
    g_conv.jobs.clear();
    std::vector<std::wstring> supported = SupportedInputExtensions();
    for (const auto& p : picked) {
        std::wstring e = FileExtension(p);
        for (const auto& s : supported) {
            if (e == s) {
                g_conv.jobs.push_back(p);
                break;
            }
        }
    }
    if (g_conv.jobs.empty()) return;
    g_conv.done = false;
    g_conv.summary.clear();
    g_conv.results.clear();
    g_conv.dispNames.clear();
    g_conv.dispFmt = -1;
    g_conv.tagsUtf8.clear();
    g_conv.tagsBuf[0] = '\0';
    g_showConvert = true;
}

// ---------------------------------------------------------------- CLI dispatch
static bool IsCliVerb(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--to-ape" || a == L"--info" || a == L"--device-list" || a == L"--help" ||
            a == L"-h" || a == L"/?" || a == L"-t" || a == L"--threads" || a == L"--to" ||
            a == L"--flac-level" || a == L"--quality" || a == L"--bitrate" || a == L"--wav-bits" ||
            a == L"--no-color" || a == L"--ascii" || a == L"--delete-original" ||
            (a.size() > 2 && a[0] == L'-' && a[1] == L'c' && iswdigit(a[2])) ||
            a.compare(0, 10, L"--threads=") == 0)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------- WndProc
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // disable ALT menu
        break;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring p(len + 1, 0);
            DragQueryFileW(drop, i, p.data(), len + 1);
            p.resize(len);
            g_pendingDrops.push_back(p);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_SETTINGCHANGE:
        // Windows broadcasts this with lParam="ImmersiveColorSet" when the
        // user flips Settings > Personalization > Colors > Choose your mode.
        if (g_followSystemTheme && lParam &&
            lstrcmpW((LPCWSTR)lParam, L"ImmersiveColorSet") == 0) {
            SyncThemeWithSystem();
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- DX11 helpers
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
            &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// ---------------------------------------------------------------- ImGui UI
static const char* LoopName(int m) {
    return m == 1 ? "Loop: All" : (m == 2 ? "Loop: One" : "Loop: Off");
}

// Reads the Windows "Choose your mode" app setting. Returns true for Light,
// false for Dark (or when the key is missing, e.g. older Windows).
static bool SystemWantsLightTheme() {
    HKEY h = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                            0, KEY_READ, &h);
    if (rc != ERROR_SUCCESS) return false;
    DWORD val = 1;
    DWORD size = sizeof(val);
    DWORD type = 0;
    rc = RegQueryValueExW(h, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&val, &size);
    RegCloseKey(h);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return false;
    return val != 0;
}

// (Re)applies the selected theme, preserving DPI scaling.
static void ApplyTheme() {
    if (g_lightTheme) ImGui::StyleColorsLight();
    else ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(g_dpiScale);
    style.FontScaleDpi = g_dpiScale;
    // Match the native title bar / window chrome to the theme.
    if (g_hwnd) {
        BOOL useDark = g_lightTheme ? FALSE : TRUE;
        DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    }
}

// Re-reads the OS theme when following the system. Applies only on change.
static void SyncThemeWithSystem() {
    bool wantLight = SystemWantsLightTheme();
    if (wantLight != g_lightTheme) {
        g_lightTheme = wantLight;
        ApplyTheme();
    } else if (g_hwnd) {
        // Ensure chrome is correct even if ImGui style already matches
        // (e.g. first call after window creation).
        BOOL useDark = g_lightTheme ? FALSE : TRUE;
        DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    }
}

// Metadata block shown under the cover art for the current track.
static void DrawTrackDetails() {
    if (!g_hasCurrent) {
        ImGui::TextDisabled("No track loaded.");
        return;
    }
    const AudioTrackInfo& i = g_player.Info();
    if (!i.title.empty()) {
        ImGui::TextWrapped("%s", WideToUtf8(i.title).c_str());
    } else {
        ImGui::TextWrapped("%s", WideToUtf8(BaseName(i.path)).c_str());
    }
    if (!i.artist.empty()) ImGui::TextWrapped("Artist: %s", WideToUtf8(i.artist).c_str());
    if (!i.album.empty()) ImGui::TextWrapped("Album: %s", WideToUtf8(i.album).c_str());
    ImGui::Separator();
    ImGui::TextUnformatted(WideToUtf8(i.formatName).c_str());
    ImGui::Text("%d Hz, %d-bit, %d ch", i.sampleRate, i.bits, i.channels);
    if (i.totalBlocks > 0) {
        int secs = i.sampleRate > 0 ? (int)(i.totalBlocks / i.sampleRate) : 0;
        ImGui::Text("Length: %d:%02d", secs / 60, secs % 60);
    } else {
        ImGui::TextUnformatted("Length: unknown");
    }
    if (i.level != 0)
        ImGui::TextWrapped("Level: %s", WideToUtf8(CompressionName(i.level)).c_str());
    if (i.avgBitrate > 0) ImGui::Text("Bitrate: ~%lld kbps", (long long)i.avgBitrate);
    if (!g_detailGenre.empty()) ImGui::TextWrapped("Genre: %s", g_detailGenre.c_str());
    if (!g_detailYear.empty()) ImGui::TextWrapped("Year: %s", g_detailYear.c_str());
    if (!g_detailTrack.empty()) ImGui::TextWrapped("Track: %s", g_detailTrack.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("File:");
    ImGui::TextWrapped("%s", WideToUtf8(BaseName(i.path)).c_str());
}

// Vector transport symbols. The default ImGui font has no media glyphs, so
// these draw with the window draw list instead of text. Square button with
// a drawn symbol + tooltip; returns true when pressed.
enum class Sym { Play, Pause, Stop, Prev, Next };

static bool SymbolButton(const char* id, Sym s, const char* tip, bool dim = false) {
    float hgt = ImGui::GetFrameHeight();
    bool pressed = ImGui::Button(id, ImVec2(hgt, hgt));
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    float r = (mx.y - mn.y) * 0.5f - 5.0f;
    if (r < 2.0f) r = 2.0f;
    const float th = 2.0f; // bar/stroke thickness
    switch (s) {
    case Sym::Play: {
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r),
                              ImVec2(c.x + r * 0.9f, c.y), col);
        break;
    }
    case Sym::Pause: {
        float w = r * 0.55f, gap = r * 0.35f;
        dl->AddRectFilled(ImVec2(c.x - gap - w, c.y - r), ImVec2(c.x - gap, c.y + r), col);
        dl->AddRectFilled(ImVec2(c.x + gap, c.y - r), ImVec2(c.x + gap + w, c.y + r), col);
        break;
    }
    case Sym::Stop: {
        dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f),
                          ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), col);
        break;
    }
    case Sym::Prev: {
        dl->AddRectFilled(ImVec2(c.x - r - 1.0f, c.y - r), ImVec2(c.x - r + th, c.y + r), col);
        dl->AddTriangleFilled(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r, c.y + r),
                              ImVec2(c.x - r + th + 1.0f, c.y), col);
        break;
    }
    case Sym::Next: {
        dl->AddRectFilled(ImVec2(c.x + r - th, c.y - r), ImVec2(c.x + r + 1.0f, c.y + r), col);
        dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r),
                              ImVec2(c.x + r - th - 1.0f, c.y), col);
        break;
    }
    }
    return pressed;
}

static void DrawMainUI() {    PollCover(); // adopt any finished background cover decode
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;
    // No vertical window padding: the bottom rows sit flush with the
    // window edge (side padding preserved, DPI-scaled).
    ImVec2 hostPad = ImGui::GetStyle().WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(hostPad.x, 0.0f));
    ImGui::Begin("AudioPlayer", nullptr, hostFlags);
    ImGui::PopStyleVar();

    // ---- menu bar ----
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open files...", "Ctrl+O")) {
                std::vector<std::wstring> picked;
                if (OpenFilesDialog(picked, AudioFileFilter(),
                                    L"Open audio files - AudioPlayer")) {
                    bool added = false;
                    AddPaths(picked, added);
                    if (added && !g_hasCurrent) PlayAt(0);
                }
            }
            if (ImGui::MenuItem("Add folder...")) {
                std::wstring dir;
                if (BrowseFolder(dir)) {
                    bool added = false;
                    AddPaths({dir}, added);
                    if (added && !g_hasCurrent) PlayAt(0);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Convert...")) {
                std::vector<std::wstring> picked;
                if (OpenFilesDialog(picked, L"Audio (*.wav,*.flac,*.ape,*.mp3,*.ogg,*.opus)\0*.wav;*.flac;*.ape;*.mp3;*.ogg;*.opus\0All files (*.*)\0*.*\0",
                                    L"Pick files to convert - AudioPlayer"))
                    OpenConvertFor(picked);
            }
            if (ImGui::MenuItem("Track details...")) {
                if (g_hasCurrent && g_pos < g_order.size()) {
                    g_detailsText =
                        BuildDetailsText(g_tracks[(size_t)g_order[g_pos]].path.c_str());
                    g_showDetails = true;
                }
            }
            if (ImGui::BeginMenu("Theme")) {
                if (ImGui::MenuItem("Follow System", nullptr, g_followSystemTheme)) {
                    g_followSystemTheme = !g_followSystemTheme;
                    if (g_followSystemTheme) SyncThemeWithSystem();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Dark", nullptr, !g_followSystemTheme && !g_lightTheme)) {
                    g_followSystemTheme = false;
                    if (g_lightTheme) {
                        g_lightTheme = false;
                        ApplyTheme();
                    }
                }
                if (ImGui::MenuItem("Light", nullptr, !g_followSystemTheme && g_lightTheme)) {
                    g_followSystemTheme = false;
                    if (!g_lightTheme) {
                        g_lightTheme = true;
                        ApplyTheme();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            bool running = g_player.DeviceRunning() && !g_player.Paused();
            if (ImGui::MenuItem(running ? "Pause" : "Play", "Space")) DoPlayPause();
            if (ImGui::MenuItem("Stop")) StopPlayback();
            if (ImGui::MenuItem("Next", "Ctrl+N") && !g_order.empty()) {
                size_t n = g_pos + 1;
                if (n >= g_order.size()) n = (g_loop == 1) ? 0 : g_order.size() - 1;
                PlayAt(n);
            }
            if (ImGui::MenuItem("Previous", "Ctrl+P") && !g_order.empty()) {
                size_t n = (g_pos > 0) ? g_pos - 1 : 0;
                PlayAt(n);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(LoopName(g_loop))) g_loop = (g_loop + 1) % 3;
            if (ImGui::MenuItem(g_shuffle ? "Shuffle: On" : "Shuffle: Off")) {
                g_shuffle = !g_shuffle;
                int cur = (g_hasCurrent && g_pos < g_order.size()) ? g_order[g_pos] : -1;
                g_order.clear();
                for (int i = 0; i < (int)g_tracks.size(); i++) g_order.push_back(i);
                if (g_shuffle) ShuffleOrder(g_order);
                g_pos = 0;
                if (cur >= 0)
                    for (size_t i = 0; i < g_order.size(); i++)
                        if (g_order[i] == cur) {
                            g_pos = i;
                            break;
                        }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) g_showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f)); // top breathing room (window top padding is 0)

    // ---- playback polling ----
    if (g_hasCurrent) {
        if (g_player.Finished()) {
            AdvanceAfterEnd();
        } else if (g_player.DeviceRunning()) {
            int64_t total = g_player.Info().totalBlocks;
            int64_t pos = g_player.Position();
            if (total > 0) g_seekPos = (int)(pos * 1000 / total);
            SetState(g_player.Paused() ? "Paused" : "Playing");
        }
    }

    // ---- now playing ----
    if (g_hasCurrent && g_pos < g_order.size()) {
        const GuiTrack& t = g_tracks[(size_t)g_order[g_pos]];
        ImGui::Text("[%u/%u] %s", (unsigned)(g_pos + 1), (unsigned)g_order.size(),
                    t.dispUtf8.c_str());
    } else {
        ImGui::TextDisabled("No track - open files, add a folder, or drop audio files here.");
    }
    ImGui::Separator();

    // ---- playlist + cover side by side ----
    float coverPane = 220.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Auto-fill: leftover space after the bottom rows feeds back into the
    // list height, converging to a zero gap at the window bottom (any DPI).
    static float s_fillExtra = 0.0f;
    float listH = avail.y - 100.0f + s_fillExtra;
    if (listH < 120.0f) listH = 120.0f;

    ImGui::BeginChild("playlist", ImVec2(avail.x - coverPane - 8.0f, listH), true);
    if (ImGui::BeginTable("tracks", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Title");
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin((int)g_order.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++) {
                const GuiTrack& t = g_tracks[(size_t)g_order[(size_t)r]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool sel = (r == g_selectedRow);
                char num[16];
                snprintf(num, sizeof(num), "%d", r + 1);
                ImGui::PushID(r);
                if (ImGui::Selectable(num, sel,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowDoubleClick)) {
                    g_selectedRow = r;
                    if (ImGui::IsMouseDoubleClicked(0)) PlayAt((size_t)r);
                }
                if (g_scrollToCurrent && g_hasCurrent && (size_t)r == g_pos) {
                    ImGui::SetScrollHereY(0.5f);
                }
                if (ImGui::BeginPopupContextItem("rowmenu")) {
                    if (ImGui::MenuItem("Play")) PlayAt((size_t)r);
                    if (ImGui::MenuItem("Track details...")) {
                        g_detailsText = BuildDetailsText(
                            g_tracks[(size_t)g_order[(size_t)r]].path.c_str());
                        g_showDetails = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove from list", "Del")) RemoveRow(r);
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(t.dispUtf8.c_str());
                ImGui::TableNextColumn();
                char dur[16]; // stack buffer: no per-row heap alloc at 60fps
                snprintf(dur, sizeof(dur), "%d:%02d", t.secs / 60, t.secs % 60);
                ImGui::TextUnformatted(dur);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    // keyboard: Enter = play, Del = remove
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            if (g_selectedRow >= 0) PlayAt((size_t)g_selectedRow);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (g_selectedRow >= 0) RemoveRow(g_selectedRow);
        }
    }
    // right-click on empty space: add files/folder
    if (ImGui::BeginPopupContextWindow("listempty", ImGuiPopupFlags_MouseButtonRight |
                                                       ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add files...")) {
            std::vector<std::wstring> picked;
            if (OpenFilesDialog(picked, AudioFileFilter(),
                                L"Open audio files - AudioPlayer")) {
                bool added = false;
                AddPaths(picked, added);
                if (added && !g_hasCurrent) PlayAt(0);
            }
        }
        if (ImGui::MenuItem("Add folder...")) {
            std::wstring dir;
            if (BrowseFolder(dir)) {
                bool added = false;
                AddPaths({dir}, added);
                if (added && !g_hasCurrent) PlayAt(0);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    g_scrollToCurrent = false;

    ImGui::SameLine();
    ImGui::BeginChild("cover", ImVec2(coverPane, listH), true);
    ImGui::TextDisabled("Cover art");
    ImGui::Separator();
    if (g_coverSRV && g_coverW > 0 && g_coverH > 0) {
        float maxW = coverPane - 16.0f;
        float maxH = listH - 320.0f; // leave room for the details block below
        if (maxH < 100.0f) maxH = 100.0f;
        float dw = maxW, dh = maxW * (float)g_coverH / (float)g_coverW;
        if (dh > maxH) {
            dh = maxH;
            dw = maxH * (float)g_coverW / (float)g_coverH;
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        ImGui::Image((ImTextureID)g_coverSRV, ImVec2(dw, dh));
    } else {
        ImGui::TextDisabled("No cover art");
    }
    ImGui::Spacing();
    ImGui::Separator();
    DrawTrackDetails();
    ImGui::EndChild();

    // ---- transport (loop text + toggles; media symbols live in the bottom row) ----
    bool running = g_hasCurrent && g_player.DeviceRunning() && !g_player.Paused();
    if (ImGui::Button(LoopName(g_loop), ImVec2(100, 0))) g_loop = (g_loop + 1) % 3;
    ImGui::SameLine();
    if (ImGui::Checkbox("Shuffle", &g_shuffle)) {
        int cur = (g_hasCurrent && g_pos < g_order.size()) ? g_order[g_pos] : -1;
        g_order.clear();
        for (int i = 0; i < (int)g_tracks.size(); i++) g_order.push_back(i);
        if (g_shuffle) ShuffleOrder(g_order);
        g_pos = 0;
        if (cur >= 0)
            for (size_t i = 0; i < g_order.size(); i++)
                if (g_order[i] == cur) {
                    g_pos = i;
                    break;
                }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Mute", &g_muted)) g_player.SetMuted(g_muted);
    ImGui::SameLine(0, 12.0f); // volume joins the transport row, right of mute

    // ---- volume + speed (same row as loop/shuffle/mute; slider fills) ----
    {
        int v = (int)(g_volume * 100.0f + 0.5f);
        // Fill the row: reserve the speed cluster's width, slider takes the rest.
        ImGuiStyle& vst = ImGui::GetStyle();
        float followW = ImGui::CalcTextSize("Speed:").x + 90.0f + ImGui::CalcTextSize("x").x +
                        ImGui::GetFrameHeight() + ImGui::CalcTextSize("Preserve pitch").x +
                        vst.ItemSpacing.x * 4.0f + vst.ItemInnerSpacing.x + 8.0f;
        float volW = ImGui::GetContentRegionAvail().x - followW;
        if (volW < 100.0f) volW = 100.0f;
        ImGui::SetNextItemWidth(volW);
        if (ImGui::SliderInt("##vol", &v, 0, 150, "%d%%")) {
            g_volume = v / 100.0f;
            if (g_volume < 0) g_volume = 0;
            if (g_volume > 1.5f) g_volume = 1.5f;
            g_player.SetVolume(g_volume);
            if (v > 0) {
                g_muted = false;
                g_player.SetMuted(false);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume");
        ImGui::SameLine(0, 12.0f); // gap between volume slider and speed
        ImGui::TextUnformatted("Speed:");
        ImGui::SameLine();
        static char speedBuf[16] = "1.00";
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputText("##speed", speedBuf, sizeof(speedBuf),
                         ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        bool commit = ImGui::IsItemDeactivatedAfterEdit();
        auto fmtSpeed = [](char* b, size_t n, float s) {
            // %.2f for human range, %g so huge values still fit the box.
            if (s < 1000.0f) snprintf(b, n, "%.2f", (double)s);
            else snprintf(b, n, "%.4g", (double)s);
        };
        if (commit) {
            // Committed via Enter or focus loss: parse and apply.
            // Non-positive/non-finite input is rejected (revert).
            char* end = nullptr;
            float sv = strtof(speedBuf, &end);
            if (end == speedBuf || !(sv > 0.0f) || !(sv < FLT_MAX)) sv = g_player.Speed();
            else g_player.SetSpeed(sv);
            fmtSpeed(speedBuf, sizeof(speedBuf), g_player.Speed());
        } else if (!ImGui::IsItemActive()) {
            // Not being edited: mirror the player (CLI nudges show up here).
            fmtSpeed(speedBuf, sizeof(speedBuf), g_player.Speed());
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        if (ImGui::Checkbox("Preserve pitch", &g_preservePitch))
            g_player.SetPreservePitch(g_preservePitch);
    }

    // ---- seek (bottom row: stop, play/pause, prev/next, slider) ----
    {
        if (SymbolButton("##stop", Sym::Stop, "Stop")) StopPlayback();
        ImGui::SameLine();
        if (SymbolButton("##playpause", running ? Sym::Pause : Sym::Play,
                         running ? "Pause (Space)" : "Play (Space)"))
            DoPlayPause();
        ImGui::SameLine();
        if (SymbolButton("##prev", Sym::Prev, "Previous") && !g_order.empty()) {
            size_t n = (g_pos > 0) ? g_pos - 1 : 0;
            PlayAt(n);
        }
        ImGui::SameLine();
        if (SymbolButton("##next", Sym::Next, "Next") && !g_order.empty()) {
            size_t n = g_pos + 1;
            if (n >= g_order.size()) n = (g_loop == 1) ? 0 : g_order.size() - 1;
            PlayAt(n);
        }
        ImGui::SameLine();
        char time[64] = "0:00 / 0:00";
        int64_t totalBlocks = g_hasCurrent ? g_player.Info().totalBlocks : 0;
        if (g_hasCurrent) {
            int rate = g_player.Info().sampleRate;
            int cur = rate > 0 ? (int)(g_player.Position() / rate) : 0;
            if (totalBlocks > 0) {
                int tot = rate > 0 ? (int)(totalBlocks / rate) : 0;
                snprintf(time, sizeof(time), "%d:%02d / %d:%02d", cur / 60, cur % 60, tot / 60,
                         tot % 60);
            } else {
                snprintf(time, sizeof(time), "%d:%02d", cur / 60, cur % 60);
            }
        }
        ImGui::SetNextItemWidth(-112.0f);
        if (totalBlocks > 0) {
            if (ImGui::SliderInt("##seek", &g_seekPos, 0, 1000, "")) {
                // live scrub while dragging
                if (g_hasCurrent) g_player.SeekTo(totalBlocks * g_seekPos / 1000);
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::SliderInt("##seek", &g_seekPos, 0, 1000, "");
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(time);
    }

    // Leftover bottom space feeds the list height above: converges to flush.
    s_fillExtra += ImGui::GetContentRegionAvail().y;
    if (s_fillExtra < -60.0f) s_fillExtra = -60.0f;
    if (s_fillExtra > 200.0f) s_fillExtra = 200.0f;

    // ---- global shortcuts (when not typing) ----
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !g_showConvert && !g_showDetails && !g_showAbout) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) DoPlayPause();
        if (ImGui::IsKeyPressed(ImGuiKey_N)) {
            if (!g_order.empty()) {
                size_t n = g_pos + 1;
                if (n >= g_order.size()) n = (g_loop == 1) ? 0 : g_order.size() - 1;
                PlayAt(n);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_P)) {
            if (!g_order.empty()) {
                size_t n = (g_pos > 0) ? g_pos - 1 : 0;
                PlayAt(n);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            g_muted = !g_muted;
            g_player.SetMuted(g_muted);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_L)) g_loop = (g_loop + 1) % 3;
    }

    // ---- convert modal ----
    if (g_showConvert) ImGui::OpenPopup("Convert");
    // Fixed width (no AlwaysAutoResize): auto-fit snapped the modal narrower /
    // wider as progress text and rows changed width mid-convert.
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Convert", nullptr, 0)) {
        ConvState* st = &g_conv;
        ImGui::Text("%u file(s) queued.", (unsigned)st->jobs.size());
        if (!st->running.load()) {
            ImGui::Combo("Output format", &st->formatIdx, kConvFormatNames, 6);
            OutFormat fmt = kConvFormats[st->formatIdx];
            if (fmt == OutFormat::APE) {
                static int levelIdx = 4;
                const char* levels[] = {"Fast (1000)", "Normal (2000)", "High (3000)",
                                        "Extra High (4000)", "Insane (5000)"};
                ImGui::Combo("Compression", &levelIdx, levels, 5);
                st->level = 1000 * (levelIdx + 1);
                if (st->level == 5000) ImGui::TextDisabled("Insane is slow - be patient.");
            } else if (fmt == OutFormat::FLAC) {
                ImGui::SliderInt("Compression level", &st->flacLevel, 0, 8);
            } else if (fmt == OutFormat::VORBIS) {
                ImGui::SliderFloat("Quality", &st->vorbisQ, -0.1f, 1.0f, "%.1f");
            } else if (fmt == OutFormat::MP3) {
                ImGui::SliderInt("Bitrate (kbps, 0 = 192)", &st->bitrateKbps, 0, 320);
            } else if (fmt == OutFormat::OPUS) {
                ImGui::SliderInt("Bitrate (kbps, 0 = 96)", &st->bitrateKbps, 0, 256);
            } else {
                static int wavIdx = 0;
                const char* wavBits[] = {"16-bit", "24-bit"};
                ImGui::Combo("WAV depth", &wavIdx, wavBits, 2);
                st->wavBits = wavIdx == 1 ? 24 : 16;
                ImGui::TextDisabled("WAV: tags embedded when present.");
            }
            ImGui::InputInt("Threads (0 = auto)", &st->threads);
            if (ImGui::InputText("Tags (Artist=X|Album=Y)", st->tagsBuf, sizeof(st->tagsBuf)))
                st->tagsUtf8 = st->tagsBuf;
            ImGui::Checkbox("Delete originals (to Recycle Bin) after success", &st->delOrig);
        } else {
            ImGui::Text("Format: %s", kConvFormatNames[st->formatIdx]);
            ImGui::Text("Threads: %d", st->threads);
            ImGui::Text("Tags: %s", st->tagsUtf8.empty() ? "(none)" : st->tagsUtf8.c_str());
        }
        ImGui::Separator();
        ImGui::BeginChild("convfiles", ImVec2(-1, 110), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        // Cache output basenames while the format/job list is unchanged:
        // rebuilding Wide strings every frame is pure waste.
        if (st->dispFmt != st->formatIdx || st->dispNames.size() != st->jobs.size()) {
            st->dispNames.clear();
            st->dispNames.reserve(st->jobs.size());
            for (const auto& j : st->jobs)
                st->dispNames.push_back(
                    WideToUtf8(BaseName(AutoOutName(j, kConvFormats[st->formatIdx]))));
            st->dispFmt = st->formatIdx;
        }
        for (size_t i = 0; i < st->jobs.size(); i++) {
            bool cur = st->running.load() && (int)i == st->curIdx.load();
            ImGui::Text("%s %s", cur ? ">" : " ", st->dispNames[i].c_str());
        }
        ImGui::EndChild();
        if (st->running.load()) {
            ImGui::ProgressBar(st->frac.load(), ImVec2(-1, 0));
            std::string d;
            {
                std::lock_guard<std::mutex> lk(st->detailMtx);
                d = st->detail;
            }
            int pct = (int)(st->frac.load() * 100.0f + 0.5f);
            ImGui::Text("Converting %d/%u: %s (%d%%)", st->curIdx.load() + 1,
                        (unsigned)st->jobs.size(),
                        st->curIdx.load() < (int)st->jobs.size()
                            ? WideToUtf8(BaseName(st->jobs[(size_t)st->curIdx.load()])).c_str()
                            : "",
                        pct);
            if (!d.empty()) ImGui::TextDisabled("%s", d.c_str());
            if (ImGui::Button("Cancel")) ConvCancel(st);
        } else if (!st->done.load()) {
            if (ImGui::Button("Convert")) ConvStart(st);
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                ConvJoin(st);
                g_showConvert = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Text("%s", st->summary.c_str());
            if (ImGui::Button("Close")) {
                ConvJoin(st);
                g_showConvert = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // ---- details modal ----
    if (g_showDetails) ImGui::OpenPopup("Track details");
    ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Track details", nullptr, 0)) {
        ImGui::BeginChild("detailstext", ImVec2(-1, -30), true);
        ImGui::TextUnformatted(g_detailsText.c_str());
        ImGui::EndChild();
        if (ImGui::Button("Close")) {
            g_showDetails = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- about modal ----
    if (g_showAbout) ImGui::OpenPopup("About AudioPlayer");
    if (ImGui::BeginPopupModal("About AudioPlayer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("AudioPlayer v2\nPlays and converts WAV, FLAC, APE, MP3, Ogg and Opus.\n"
                               "ImGui frontend (Win32 + DirectX11).\nBuilt with MinGW-w64.");
        if (ImGui::Button("Close")) {
            g_showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

// ---------------------------------------------------------------- entry
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show) {
    (void)hPrev;
    (void)cmd;
    srand((unsigned)time(nullptr));

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv && IsCliVerb(argc, argv)) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        bool piped =
            hOut && hOut != INVALID_HANDLE_VALUE && GetFileType(hOut) != FILE_TYPE_UNKNOWN;
        if (piped) {
            int fd = _open_osfhandle((intptr_t)hOut, _O_TEXT);
            if (fd >= 0) _dup2(fd, 1);
            HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
            if (hErr && hErr != INVALID_HANDLE_VALUE) {
                int fd2 = _open_osfhandle((intptr_t)hErr, _O_TEXT);
                if (fd2 >= 0) _dup2(fd2, 2);
            }
        } else {
            if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            freopen_s(&f, "CONIN$", "r", stdin);
        }
        int rc = ConsoleMain(argc, argv);
        LocalFree(argv);
        if (!piped) FreeConsole();
        return rc;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // Explicit taskbar identity so the taskbar/Alt-Tab name stays
    // "AudioPlayer" no matter what the exe is renamed to (and so stale
    // pins from the old AudioPlayer.exe days don't capture the window).
    SetCurrentProcessExplicitAppUserModelID(L"AudioPlayer");
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale =
        ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInst, nullptr, nullptr, nullptr,
                      nullptr, L"AudioPlayer_IMGUI", nullptr};
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName,
                                  L"AudioPlayer",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  (int)(1000 * main_scale), (int)(660 * main_scale), nullptr,
                                  nullptr, hInst, nullptr);
    if (!hwnd) {
        LocalFree(argv);
        CoUninitialize();
        return 1;
    }
    g_hwnd = hwnd;
    DragAcceptFiles(hwnd, TRUE);
    if (wc.hIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIcon);
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, hInst);
        LocalFree(argv);
        CoUninitialize();
        return 1;
    }

    ::ShowWindow(hwnd, show);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // keep it portable: no imgui.ini beside the exe

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    g_dpiScale = main_scale;
    if (g_followSystemTheme) g_lightTheme = SystemWantsLightTheme();
    ApplyTheme();

    // Queue files passed on the command line.
    if (argv) {
        std::vector<std::wstring> paths;
        for (int i = 1; i < argc; i++) paths.push_back(argv[i]);
        LocalFree(argv);
        argv = nullptr;
        if (!paths.empty()) {
            bool added = false;
            AddPaths(paths, added);
            if (added) PlayAt(0);
        }
    }

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Files dropped onto the window (posted from WndProc).
        if (!g_pendingDrops.empty()) {
            bool added = false;
            AddPaths(g_pendingDrops, added);
            g_pendingDrops.clear();
            if (added && !g_hasCurrent) PlayAt(0);
        }

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawMainUI();

        ImVec4 clear_color = g_lightTheme ? ImVec4(0.82f, 0.82f, 0.85f, 1.00f)
                                          : ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        ImGui::Render();
        const float clear[4] = {clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                                clear_color.z * clear_color.w, clear_color.w};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0); // vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ConvCancel(&g_conv);
    ConvJoin(&g_conv);
    g_coverShutdown = true;
    g_player.Close();
    FreeCover();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
