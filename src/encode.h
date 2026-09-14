// Encode - any supported input -> any supported output. The pipeline is
// always PCM: each input decodes to 16-bit frames, each output consumes
// 16-bit frames. See encode.h.
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

// Target container/codec. FLAC/MP3/VORBIS/OPUS engines arrive in later
// phases; FormatAvailable() reports what this build can actually write.
enum class OutFormat { APE, WAV, FLAC, MP3, VORBIS, OPUS };

struct EncodeOptions {
    OutFormat format = OutFormat::APE;
    int level = 5000;                    // APE_COMPRESSION_LEVEL_*, default Insane
    int threads = 0;                     // 0 = auto (CPU count, clamped 1..8)
    std::wstring tagOverride;            // optional: L"Artist=X|Album=Y" (wins over auto)
    bool deleteOriginal = false;         // recycle the input after a GOOD encode
    int flacLevel = 5;                   // 0..8 (FLAC output)
    float vorbisQuality = 0.4f;          // -0.1..1.0 (Vorbis output)
    int opusBitrate = 96000;             // bits/sec (Opus output)
    int mp3Bitrate = 192;                // kbps CBR (MP3 output)
    int wavBits = 16;                    // 16 or 24 (WAV output; 24 pads the 16-bit feed)
};

// Optional progress/cancel sink. Null = styled console output, no cancel.
struct IEncodeProgress {
    virtual void OnProgress(double frac, const char* detail) = 0;
    virtual bool Cancelled() = 0;
    virtual ~IEncodeProgress() {}
};

// Exit codes: 0 ok, 1 usage/io, 2 invalid input, 4 encode failure, 5 cancelled.
int EncodeToFile(const std::wstring& inPath, const std::wstring& outPath,
                 const EncodeOptions& opt, IEncodeProgress* sink = nullptr);

// Back-compat wrapper: forces APE output (used by --to-ape and old callers).
int EncodeToApe(const std::wstring& inPath, const std::wstring& outPath,
                const EncodeOptions& opt, IEncodeProgress* sink = nullptr);

// True when this build has an encoder for the format.
bool FormatAvailable(OutFormat fmt);

// Default output: same folder/name with the format's extension.
std::wstring AutoOutName(const std::wstring& inPath, OutFormat fmt);

// Back-compat: same folder/name with .ape extension.
inline std::wstring AutoApeName(const std::wstring& inPath) {
    return AutoOutName(inPath, OutFormat::APE);
}

// "ape|wav|flac|mp3|ogg|opus" (case-insensitive) -> format. False if unknown.
bool ParseOutFormat(const std::wstring& s, OutFormat& fmt);

// e.g. L"APE (.ape)". Short UI label, e.g. L"APE".
const wchar_t* OutFormatName(OutFormat fmt);
const wchar_t* OutFormatShortName(OutFormat fmt);

// e.g. L".ape" (L".ogg" for Vorbis, L".opus" for Opus).
const wchar_t* OutFormatExtension(OutFormat fmt);

// "Artist=X|Album=Y" -> [(Artist,X),(Album,Y)]. False on malformed input.
bool ParseTagString(const std::wstring& s,
                    std::vector<std::pair<std::wstring, std::wstring>>& fields);

// Writes fields to an existing .ape file. Warns (returns 0) on failure -
// tags must never fail an otherwise good encode.
int ApplyApeTags(const std::wstring& apeFile,
                 const std::vector<std::pair<std::wstring, std::wstring>>& fields);

// e.g. 5000 -> L"Fast (1000)".
const wchar_t* CompressionName(int level);

// Lowercased file extension including dot (L".mp3"), or L"".
std::wstring FileExtension(const std::wstring& path);

// Supported conversion inputs in this build (".wav", ".mp3", ...).
std::vector<std::wstring> SupportedInputExtensions();
