// Shared PCM pipeline pieces (implemented in encode_pcm.cpp).
// Every conversion input decodes to signed 16-bit interleaved frames;
// every output engine consumes them.
#pragma once

#include "ma_config.h"
#include "miniaudio.h"
#include "encode.h"

#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <cstdint>

constexpr ma_uint64 kUnknownTotal = (ma_uint64)~(ma_uint64)0;

std::string U8(const wchar_t* w);

class PcmSource {
public:
    virtual ~PcmSource() {}
    virtual bool open(const std::wstring& path, std::wstring& err) = 0;
    virtual int channels() const = 0;
    virtual int sampleRate() const = 0;
    virtual ma_uint64 totalFrames() const = 0;
    virtual bool depthReduced() const { return false; } // >16-bit source squeezed to s16
    // Reads up to wantFrames; returns frames read (0 = EOF), <0 on error.
    virtual int64_t readS16(int16_t* dst, uint64_t wantFrames) = 0;
    // Random access for playback (block = frame). Default: reopen + skip.
    // Returns false when seeking is impossible.
    virtual bool seekBlock(int64_t block);
    // Format-specific tag readers (Vorbis comments etc.); default: none.
    virtual void extraTags(const std::wstring& path,
                           std::vector<std::pair<std::wstring, std::wstring>>& fields) const {
        (void)path;
        (void)fields;
    }
};

// Construct (but do not open) a source for the path. Null when the
// extension is unknown.
std::unique_ptr<PcmSource> CreatePcmSource(const std::wstring& path);

// Null + err text when the extension is unknown or the file won't open.
std::unique_ptr<PcmSource> OpenPcmSource(const std::wstring& path, std::wstring& err);

// Frame-based progress for manual feed loops (GUI sink or console bar).
class FrameProgress {
public:
    FrameProgress(IEncodeProgress* sink, ma_uint64 total);
    void update(ma_uint64 done);
    bool cancelled();
    void finish();

private:
    unsigned long long m_start;
    IEncodeProgress* m_sink;
    ma_uint64 m_total;
};

// Stereo downmix wrapper for >2ch sources feeding lossy encoders.
// L = mean of even channels, R = mean of odd channels.
class DownmixSource : public PcmSource {
public:
    explicit DownmixSource(std::unique_ptr<PcmSource> inner);
    bool open(const std::wstring& path, std::wstring& err) override;
    int channels() const override { return 2; }
    int sampleRate() const override;
    ma_uint64 totalFrames() const override;
    bool depthReduced() const override;
    bool seekBlock(int64_t block) override;
    int64_t readS16(int16_t* dst, uint64_t wantFrames) override;
    void extraTags(const std::wstring& path,
                   std::vector<std::pair<std::wstring, std::wstring>>& fields) const override;

private:
    std::unique_ptr<PcmSource> m_inner;
    std::vector<int16_t> m_buf;
};

// Sample-rate wrapper (s16 -> s16) for encoders with fixed rate needs
// (Opus = 48kHz, LAME = standard MPEG rates).
class ResampleSource : public PcmSource {
public:
    ResampleSource(std::unique_ptr<PcmSource> inner, int targetRate);
    ~ResampleSource() override;
    bool open(const std::wstring& path, std::wstring& err) override;
    int channels() const override;
    int sampleRate() const override { return m_target; }
    ma_uint64 totalFrames() const override { return m_outTotal; }
    bool depthReduced() const override;
    bool seekBlock(int64_t block) override;
    int64_t readS16(int16_t* dst, uint64_t wantFrames) override;
    void extraTags(const std::wstring& path,
                   std::vector<std::pair<std::wstring, std::wstring>>& fields) const override;

private:
    int64_t pullConverted(int16_t* dst, uint64_t wantFrames);
    std::unique_ptr<PcmSource> m_inner;
    int m_target;
    ma_data_converter m_conv;
    bool m_convInit = false;
    std::vector<int16_t> m_carry; // unconsumed input frames (see pullConverted)
    size_t m_carryOff = 0;        // read offset into m_carry, in frames
    size_t m_carryLen = 0;        // valid frames from m_carryOff
    std::vector<int16_t> m_out;   // reused converter scratch (no per-call alloc)
    bool m_innerEof = false;
    ma_uint64 m_outTotal = kUnknownTotal;
    ma_uint64 m_served = 0;
};
