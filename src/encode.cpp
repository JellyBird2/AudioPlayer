// Encode - any supported input -> any supported output.
//
// The pipeline is always PCM: each input decodes to 16-bit frames, each
// output consumes 16-bit frames. WAV->APE keeps the SDK fast path
// (CompressFileW) so WAV depth is preserved; everything else goes through
// the generic feed loop (no temp files). FLAC/Vorbis/Opus/MP3 engines and
// extra tag readers live in encode_extra.cpp.
#include "ma_config.h"
#include "miniaudio.h"

#include "All.h"
#include "MACLib.h"
#include "IAPETag.h"

#include "encode.h"
#include "encode_pcm.h"
#include "encode_extra.h"
#include "id3.h"
#include "ui.h"

#include <windows.h>
#include <mmreg.h>
#include <shellapi.h>
#include <stdio.h>
#include <wctype.h>
#include <thread>
#include <vector>
#include <memory>

using namespace APE;

namespace {

bool SameField(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

unsigned long long FileBytes(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

// Sends a file to the Recycle Bin (recoverable delete). No prompts, no UI.
bool RecycleFile(const wchar_t* path) {
    std::wstring from = path;
    from.push_back(L'\0'); // SHFileOperation wants double-NUL termination
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

// Progress sink for CompressFileW. Forwards to a GUI sink when present,
// otherwise styled console output.
class UIProgress : public IAPEProgressCallback {
public:
    explicit UIProgress(IEncodeProgress* sink) : m_sink(sink) { start = GetTickCount64(); }
    virtual void Progress(int nPercentageDone) APE_OVERRIDE {
        double frac = nPercentageDone / 1.e5;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        Report(frac);
    }
    virtual int GetKillFlag() APE_OVERRIDE {
        if (m_sink && m_sink->Cancelled()) return APE_KILL_FLAG_STOP;
        return APE_KILL_FLAG_CONTINUE;
    }
    void Report(double frac) {
        double elapsed = (GetTickCount64() - start) / 1000.0;
        char detail[96];
        if (frac > 0.001 && frac < 1.0) {
            double total = elapsed / frac;
            snprintf(detail, sizeof(detail), "%.0fs elapsed, ~%.0fs left", elapsed, total - elapsed);
        } else {
            snprintf(detail, sizeof(detail), "%.0fs elapsed", elapsed);
        }
        if (m_sink) m_sink->OnProgress(frac, detail);
        else UI::Progress("Encoding", frac, detail);
    }
private:
    unsigned long long start;
    IEncodeProgress* m_sink;
};

int ResolveThreads(int want) {
    if (want > 0) return want;
    unsigned n = std::thread::hardware_concurrency();
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    return (int)n;
}

// WAV -> APE fast path: the SDK handles every WAV flavor (depth preserved).
int EncodeWavFast(const std::wstring& in, const std::wstring& out, int level, int threads,
                  IEncodeProgress* sink) {
    UIProgress cb(sink);
    CAPEProgressCallbackInfo info = { &cb, APE_NULL, APE_NULL, APE_NULL };
    int ret = CompressFileW(in.c_str(), out.c_str(), level, &info, threads, APE_NULL, false);
    if (!sink) UI::EndProgress();
    if (ret != 0) {
        DeleteFileW(out.c_str());
        if (cb.GetKillFlag() != APE_KILL_FLAG_CONTINUE) return 5;
        return 4;
    }
    return 0;
}

// Generic PCM -> APE feed (any input through IAPECompress, 16-bit).
int EncodeApeFromPcm(PcmSource& src, const std::wstring& out, int level, int threads,
                     IEncodeProgress* sink) {
    int ch = src.channels(), rate = src.sampleRate();
    ma_uint64 total = src.totalFrames();
    int64_t maxBytes =
        total == kUnknownTotal ? MAX_AUDIO_BYTES_UNKNOWN : (int64_t)(total * (ma_uint64)ch * 2);

    APE::WAVEFORMATEX wfe;
    FillWaveFormatEx(&wfe, WAVE_FORMAT_PCM, rate, 16, ch);

    int err = 0;
    IAPECompress* enc = CreateIAPECompress(&err);
    if (!enc) {
        if (!sink) fwprintf(stderr, L"Cannot create encoder (error %d).\n", err);
        return 4;
    }
    int ret = enc->Start(out.c_str(), &wfe, false, maxBytes, level, APE_NULL,
                         CREATE_WAV_HEADER_ON_DECOMPRESSION);
    if (ret != 0) {
        if (!sink) fwprintf(stderr, L"Encoder failed to start (error %d).\n", ret);
        delete enc;
        DeleteFileW(out.c_str());
        return 4;
    }
    enc->SetNumberOfThreads(threads); // best effort; ignore failure

    FrameProgress prog(sink, total);
    ma_uint64 fed = 0;
    int fail = 0; // 1 = error, 2 = cancelled
    for (;;) {
        if (prog.cancelled()) { fail = 2; break; }
        int64_t avail = 0;
        unsigned char* buf = enc->LockBuffer(&avail);
        if (!buf || avail <= 0) { fail = 1; break; }
        ma_uint64 wantFrames = (ma_uint64)avail / (ma_uint64)(ch * 2);
        if (wantFrames == 0) { enc->UnlockBuffer(0, TRUE); break; }
        // Decode straight into the SDK buffer: no intermediate copy.
        // Every PcmSource chunks internally, so any wantFrames is safe.
        int64_t got = src.readS16((int16_t*)buf, wantFrames);
        if (got < 0) { enc->UnlockBuffer(0, TRUE); fail = 1; break; }
        if (got == 0) { enc->UnlockBuffer(0, TRUE); break; } // EOF
        if (enc->UnlockBuffer(got * (int64_t)ch * 2, TRUE) != 0) { fail = 1; break; }
        fed += (ma_uint64)got;
        prog.update(fed);
    }
    prog.finish();

    if (!fail && enc->Finish(APE_NULL, 0, 0) != 0) fail = 1;
    delete enc;
    if (fail) {
        DeleteFileW(out.c_str());
        return fail == 2 ? 5 : 4;
    }
    return 0;
}

// Generic PCM -> WAV (16- or 24-bit) via the miniaudio encoder.
// The pipeline feeds 16-bit frames; 24-bit output left-justifies them
// (zero-padded LSB, no invented resolution).
int EncodeWavFromPcm(PcmSource& src, const std::wstring& out, int wavBits,
                     IEncodeProgress* sink) {
    if (wavBits != 16 && wavBits != 24) wavBits = 16;
    int ch = src.channels(), rate = src.sampleRate();
    ma_format fmt = wavBits == 24 ? ma_format_s24 : ma_format_s16;
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, fmt,
                                                   (ma_uint32)ch, (ma_uint32)rate);
    ma_encoder enc;
    if (ma_encoder_init_file_w(out.c_str(), &cfg, &enc) != MA_SUCCESS) {
        if (!sink) fwprintf(stderr, L"Cannot create WAV output: %ls\n", out.c_str());
        return 4;
    }
    FrameProgress prog(sink, src.totalFrames());
    std::vector<int16_t> pcm((size_t)4096 * (size_t)ch);
    std::vector<unsigned char> pcm24; // s16 frames widened to packed s24
    if (wavBits == 24) pcm24.resize((size_t)4096 * (size_t)ch * 3);
    ma_uint64 fed = 0;
    int fail = 0;
    for (;;) {
        if (prog.cancelled()) { fail = 2; break; }
        int64_t got = src.readS16(pcm.data(), 4096);
        if (got < 0) { fail = 1; break; }
        if (got == 0) break; // EOF
        const void* data = pcm.data();
        if (wavBits == 24) {
            size_t n = (size_t)got * (size_t)ch;
            for (size_t i = 0; i < n; i++) {
                int32_t v = (int32_t)pcm[i] << 8; // s16 -> s24, padded LSB
                pcm24[i * 3 + 0] = (unsigned char)(v & 0xFF);
                pcm24[i * 3 + 1] = (unsigned char)((v >> 8) & 0xFF);
                pcm24[i * 3 + 2] = (unsigned char)((v >> 16) & 0xFF);
            }
            data = pcm24.data();
        }
        ma_uint64 written = 0;
        if (ma_encoder_write_pcm_frames(&enc, data, (ma_uint64)got, &written) != MA_SUCCESS ||
            written != (ma_uint64)got) {
            fail = 1;
            break;
        }
        fed += (ma_uint64)got;
        prog.update(fed);
    }
    prog.finish();
    ma_encoder_uninit(&enc);
    if (fail) {
        DeleteFileW(out.c_str());
        return fail == 2 ? 5 : 4;
    }
    return 0;
}

} // namespace

bool FormatAvailable(OutFormat fmt) {
    (void)fmt;
    return true; // APE/WAV/FLAC/MP3/Vorbis/Opus all ship in this build
}

std::wstring AutoOutName(const std::wstring& inPath, OutFormat fmt) {
    size_t dot = inPath.find_last_of(L'.');
    size_t sep = inPath.find_last_of(L"\\/:");
    std::wstring base =
        (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep))
            ? inPath.substr(0, dot)
            : inPath;
    return base + OutFormatExtension(fmt);
}

bool ParseOutFormat(const std::wstring& s, OutFormat& fmt) {
    std::wstring low = s;
    for (auto& c : low) c = (wchar_t)towlower(c);
    if (low == L"ape") fmt = OutFormat::APE;
    else if (low == L"wav") fmt = OutFormat::WAV;
    else if (low == L"flac") fmt = OutFormat::FLAC;
    else if (low == L"mp3") fmt = OutFormat::MP3;
    else if (low == L"ogg" || low == L"vorbis") fmt = OutFormat::VORBIS;
    else if (low == L"opus") fmt = OutFormat::OPUS;
    else return false;
    return true;
}

const wchar_t* OutFormatName(OutFormat fmt) {
    switch (fmt) {
        case OutFormat::APE: return L"APE (.ape)";
        case OutFormat::WAV: return L"WAV (.wav)";
        case OutFormat::FLAC: return L"FLAC (.flac)";
        case OutFormat::MP3: return L"MP3 (.mp3)";
        case OutFormat::VORBIS: return L"Ogg Vorbis (.ogg)";
        case OutFormat::OPUS: return L"Opus (.opus)";
        default: return L"Unknown";
    }
}

const wchar_t* OutFormatShortName(OutFormat fmt) {
    switch (fmt) {
        case OutFormat::APE: return L"APE";
        case OutFormat::WAV: return L"WAV";
        case OutFormat::FLAC: return L"FLAC";
        case OutFormat::MP3: return L"MP3";
        case OutFormat::VORBIS: return L"Vorbis";
        case OutFormat::OPUS: return L"Opus";
        default: return L"?";
    }
}

const wchar_t* OutFormatExtension(OutFormat fmt) {
    switch (fmt) {
        case OutFormat::APE: return L".ape";
        case OutFormat::WAV: return L".wav";
        case OutFormat::FLAC: return L".flac";
        case OutFormat::MP3: return L".mp3";
        case OutFormat::VORBIS: return L".ogg";
        case OutFormat::OPUS: return L".opus";
        default: return L".bin";
    }
}

std::vector<std::wstring> SupportedInputExtensions() {
    return {L".wav", L".mp3", L".flac", L".ogg", L".oga", L".opus", L".ape"};
}

const wchar_t* CompressionName(int level) {
    switch (level) {
        case 1000: return L"Fast (1000)";
        case 2000: return L"Normal (2000)";
        case 3000: return L"High (3000)";
        case 4000: return L"Extra High (4000)";
        case 5000: return L"Insane (5000)";
        default: return L"Unknown";
    }
}

std::wstring FileExtension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/:");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep))
        return std::wstring();
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

bool ParseTagString(const std::wstring& s,
                    std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    fields.clear();
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t bar = s.find(L'|', pos);
        std::wstring item = s.substr(pos, bar == std::wstring::npos ? bar : bar - pos);
        size_t eq = item.find(L'=');
        if (eq == std::wstring::npos || eq == 0) return false;
        fields.push_back({item.substr(0, eq), item.substr(eq + 1)});
        if (bar == std::wstring::npos) break;
        pos = bar + 1;
    }
    return !fields.empty();
}

int ApplyApeTags(const std::wstring& apeFile,
                 const std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    if (fields.empty()) return 0;
    int err = 0;
    IAPEDecompress* dec = CreateIAPEDecompress(apeFile.c_str(), &err, false, true, false);
    if (!dec) return 0; // tags must never fail the encode
    IAPETag* tag = dec->GetTag();
    if (tag) {
        for (const auto& kv : fields) tag->SetFieldString(kv.first.c_str(), kv.second.c_str());
        tag->Save(false);
    }
    delete dec;
    return 0;
}

namespace {

// Text tags carried from input -> output. Path-based readers run before
// the encode; source-held tags (Opus/Vorbis comments) are appended by the
// caller via feed->extraTags() after the final open.
void CollectInputTags(const std::wstring& inPath,
                      std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    fields.clear();
    std::wstring ext = FileExtension(inPath);
    if (ext == L".mp3") {
        ID3Tags id3;
        if (ReadID3Tags(inPath.c_str(), id3) && !id3.empty()) {
            auto m = id3.fields();
            for (const auto& kv : m) fields.push_back(kv);
        }
    } else if (ext == L".ape") {
        int code = 0;
        IAPEDecompress* dec = CreateIAPEDecompress(inPath.c_str(), &code, false, true, false);
        if (dec) {
            IAPETag* tag = dec->GetTag();
            if (tag) {
                static const wchar_t* names[] = {L"Title", L"Artist", L"Album", L"Genre",
                                                 L"Year", L"Track", L"Comment"};
                for (const wchar_t* n : names) {
                    wchar_t buf[1024];
                    int chars = 1024;
                    if (tag->GetFieldString(n, buf, &chars) == 0 && chars > 0 && buf[0] != 0)
                        fields.push_back({n, buf});
                }
            }
            delete dec;
        }
    } else if (ext == L".flac") {
        ReadFlacTags(inPath, fields);
    } else if (ext == L".wav") {
        ReadWavTags(inPath, fields);
    }
    // (.ogg/.opus comments arrive via feed->extraTags after the final open.)
}

void MergeManualTags(std::vector<std::pair<std::wstring, std::wstring>>& fields,
                     const std::wstring& tagOverride) {
    std::vector<std::pair<std::wstring, std::wstring>> manual;
    if (!tagOverride.empty() && !ParseTagString(tagOverride, manual)) {
        fwprintf(stderr, L"Bad -t format, want \"Name=Value|Name=Value\". Tags skipped.\n");
        return;
    }
    for (const auto& kv : manual) {
        bool replaced = false;
        for (auto& f : fields) {
            if (SameField(f.first, kv.first)) { f.second = kv.second; replaced = true; break; }
        }
        if (!replaced) fields.push_back(kv);
    }
}

const char* TagSourceName(const std::wstring& ext) {
    if (ext == L".mp3") return "copied from MP3";
    if (ext == L".ape") return "copied from APE";
    if (ext == L".flac") return "copied from FLAC";
    if (ext == L".ogg" || ext == L".oga" || ext == L".opus") return "copied from Ogg/Opus tags";
    if (ext == L".wav") return "copied from WAV";
    return "manual";
}

} // namespace

int EncodeToFile(const std::wstring& inPath, const std::wstring& outPath,
                 const EncodeOptions& opt, IEncodeProgress* sink) {
    DWORD attr = GetFileAttributesW(inPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!sink) fwprintf(stderr, L"Input not found: %ls\n", inPath.c_str());
        return 1;
    }
    if (_wcsicmp(inPath.c_str(), outPath.c_str()) == 0) {
        if (!sink) fwprintf(stderr, L"Input and output are the same file.\n");
        return 2;
    }
    if (!FormatAvailable(opt.format)) {
        if (!sink)
            fwprintf(stderr, L"%ls output is not in this build yet.\n", OutFormatName(opt.format));
        return 1;
    }

    std::wstring ext = FileExtension(inPath);
    bool supported = false;
    for (const auto& e : SupportedInputExtensions())
        if (e == ext) { supported = true; break; }
    if (!supported) {
        if (!sink) fwprintf(stderr, L"Unsupported input type (got: %ls)\n", inPath.c_str());
        return 2;
    }

    int threads = ResolveThreads(opt.threads);
    unsigned long long inBytes = FileBytes(inPath.c_str());
    bool fromLossy = (ext == L".mp3" || ext == L".ogg" || ext == L".oga" || ext == L".opus");
    bool toLossless = (opt.format == OutFormat::APE || opt.format == OutFormat::WAV ||
                       opt.format == OutFormat::FLAC);
    bool toLossy = !toLossless;

    if (!sink) {
        UI::Set(UI::BOLD_);
        printf("Convert: ");
        UI::Reset();
        printf("%s\n", U8(inPath.c_str()).c_str());
        UI::Set(UI::GRAY_);
        switch (opt.format) {
            case OutFormat::APE:
                printf("  -> %s  [APE %ls, %d thread%s]%s\n", U8(outPath.c_str()).c_str(),
                       CompressionName(opt.level), threads, threads == 1 ? "" : "s",
                       fromLossy ? "  (lossy source: quality stays as-is, file may grow)" : "");
                break;
            case OutFormat::WAV:
                printf("  -> %s  [WAV %d-bit PCM]%s\n", U8(outPath.c_str()).c_str(),
                       opt.wavBits == 24 ? 24 : 16,
                       fromLossy ? "  (lossy source: quality stays as-is, file may grow)" : "");
                break;
            case OutFormat::FLAC:
                printf("  -> %s  [FLAC level %d]%s\n", U8(outPath.c_str()).c_str(), opt.flacLevel,
                       fromLossy ? "  (lossy source: quality stays as-is, file may grow)" : "");
                break;
            case OutFormat::MP3:
                printf("  -> %s  [MP3 %d kbps CBR]\n", U8(outPath.c_str()).c_str(),
                       NearestMp3Bitrate(opt.mp3Bitrate));
                break;
            case OutFormat::VORBIS:
                printf("  -> %s  [Ogg Vorbis q%.1f]\n", U8(outPath.c_str()).c_str(),
                       (double)opt.vorbisQuality);
                break;
            case OutFormat::OPUS:
                printf("  -> %s  [Opus %d kbps]\n", U8(outPath.c_str()).c_str(),
                       opt.opusBitrate / 1000);
                break;
        }
        UI::Reset();
        if (fromLossy && toLossless) {
            UI::Set(UI::YELLOW_);
            printf("  Note: converting lossy -> lossless preserves quality but not size.\n");
            UI::Reset();
        }
        if (opt.format == OutFormat::APE && opt.level == 5000) {
            UI::Set(UI::YELLOW_);
            printf("  Insane mode is slow and memory-hungry - progress keeps moving, be patient.\n");
            UI::Reset();
        }
        fflush(stdout);
    }

    int rc;
    std::vector<std::pair<std::wstring, std::wstring>> fields;
    if (opt.format == OutFormat::APE && ext == L".wav") {
        CollectInputTags(inPath, fields); // WAV LIST tags
        MergeManualTags(fields, opt.tagOverride);
        rc = EncodeWavFast(inPath, outPath, opt.level, threads, sink); // depth-preserving
    } else {
        // Probe (open + close) for stream params, then build the final
        // wrapped chain and open it exactly once via the outermost layer.
        int probeCh = 0, probeRate = 0;
        bool probeDepth = false;
        {
            std::wstring openErr;
            std::unique_ptr<PcmSource> probe = CreatePcmSource(inPath);
            if (!probe || !probe->open(inPath, openErr)) {
                if (!sink)
                    fwprintf(stderr, L"Cannot open input: %ls (%ls)\n", inPath.c_str(),
                             probe ? openErr.c_str() : L"unsupported type");
                return 2;
            }
            probeCh = probe->channels();
            probeRate = probe->sampleRate();
            probeDepth = probe->depthReduced();
        } // probe closes here; the real chain opens once below
        if (!sink && probeDepth) {
            UI::Set(UI::YELLOW_);
            printf("  Note: >16-bit source will be stored as 16-bit in this build.\n");
            UI::Reset();
        }
        // Tags are collected BEFORE encoding: the new engines (FLAC/
        // Vorbis/Opus/MP3) embed them during the encode itself. Path-based
        // readers run now; source-held tags (Opus/Vorbis comments) are
        // picked up after the final open below.
        CollectInputTags(inPath, fields);
        // Lossy codecs take stereo/mono: downmix wider sources (noted).
        std::unique_ptr<PcmSource> owned = CreatePcmSource(inPath);
        if (!owned) {
            if (!sink) fwprintf(stderr, L"Unsupported input type.\n");
            return 2;
        }
        PcmSource* feed = owned.get();
        if (toLossy && probeCh > 2) {
            if (!sink) {
                UI::Set(UI::GRAY_);
                printf("  (%dch source downmixed to stereo for %ls output)\n", probeCh,
                       OutFormatShortName(opt.format));
                UI::Reset();
            }
            owned.reset(new DownmixSource(std::move(owned)));
            feed = owned.get();
        }
        if (opt.format == OutFormat::OPUS && probeRate != 48000) {
            if (!sink) {
                UI::Set(UI::GRAY_);
                printf("  (resampled %d Hz -> 48000 Hz for Opus)\n", probeRate);
                UI::Reset();
            }
            owned.reset(new ResampleSource(std::move(owned), 48000));
            feed = owned.get();
        }
        if (opt.format == OutFormat::MP3 && !LameRateOk(probeRate)) {
            if (!sink) {
                UI::Set(UI::GRAY_);
                printf("  (resampled %d Hz -> 44100 Hz for MP3)\n", probeRate);
                UI::Reset();
            }
            owned.reset(new ResampleSource(std::move(owned), 44100));
            feed = owned.get();
        }
        {
            std::wstring openErr;
            if (!feed->open(inPath, openErr)) {
                if (!sink)
                    fwprintf(stderr, L"Cannot open input: %ls (%ls)\n", inPath.c_str(),
                             openErr.c_str());
                return 2;
            }
        }
        int inputRate = probeRate; // pre-wrap rate, stored in the OpusHead header
        feed->extraTags(inPath, fields); // Opus/Vorbis comments held by the source
        MergeManualTags(fields, opt.tagOverride);

        switch (opt.format) {
            case OutFormat::APE:
                rc = EncodeApeFromPcm(*feed, outPath, opt.level, threads, sink);
                break;
            case OutFormat::WAV:
                rc = EncodeWavFromPcm(*feed, outPath, opt.wavBits, sink);
                break;
            case OutFormat::FLAC:
                rc = EncodeFlacFromPcm(*feed, outPath, opt.flacLevel, fields, sink);
                break;
            case OutFormat::MP3:
                rc = EncodeMp3FromPcm(*feed, outPath, opt.mp3Bitrate, fields, sink);
                break;
            case OutFormat::VORBIS:
                rc = EncodeVorbisFromPcm(*feed, outPath, opt.vorbisQuality, fields, sink);
                break;
            case OutFormat::OPUS:
                rc = EncodeOpusFromPcm(*feed, outPath, opt.opusBitrate, inputRate, fields, sink);
                break;
            default:
                rc = 4;
                break;
        }
    }
    if (rc != 0) return rc;

    // Post-pass tag writers (FLAC/Vorbis/Opus/MP3 embed tags during encode).
    if (opt.format == OutFormat::APE) ApplyApeTags(outPath, fields);
    else if (opt.format == OutFormat::WAV) WriteWavTags(outPath, fields);

    // Only ever touch the original after a fully good encode + tags.
    bool recycled = false;
    if (opt.deleteOriginal) {
        recycled = RecycleFile(inPath.c_str());
        if (!recycled && !sink)
            fwprintf(stderr, L"Warning: encoded fine but the original could not be recycled: %ls\n",
                     inPath.c_str());
    }

    if (!sink) {
        if (!fields.empty() && (opt.format == OutFormat::APE || opt.format == OutFormat::WAV)) {
            UI::Set(UI::GRAY_);
            printf("  Tags: %u field%s (%s%s).\n", (unsigned)fields.size(),
                   fields.size() == 1 ? "" : "s", TagSourceName(ext),
                   !opt.tagOverride.empty() ? " + manual" : "");
            UI::Reset();
        }
        unsigned long long outBytes = FileBytes(outPath.c_str());
        UI::Set(UI::GREEN_);
        printf("  Done: %llu -> %llu bytes", inBytes, outBytes);
        UI::Reset();
        if (inBytes > 0) printf("  (%.1f%% of original)", outBytes * 100.0 / inBytes);
        if (recycled) printf("  [original recycled]");
        printf("\n");
        fflush(stdout);
    }
    return 0;
}

int EncodeToApe(const std::wstring& inPath, const std::wstring& outPath,
                const EncodeOptions& opt, IEncodeProgress* sink) {
    EncodeOptions apeOpt = opt;
    apeOpt.format = OutFormat::APE;
    return EncodeToFile(inPath, outPath, apeOpt, sink);
}
