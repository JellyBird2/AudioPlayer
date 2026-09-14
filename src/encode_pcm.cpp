// Shared PCM pipeline: miniaudio-backed and APE-backed 16-bit sources,
// stereo downmix + resample wrappers, frame progress. See encode_pcm.h.
#include "encode_pcm.h"
#include "encode_extra.h" // OpenOpusSource hook for .opus input

#include "All.h"
#include "MACLib.h"

#include "ui.h"

#include <stdio.h>
#include <string.h>

using namespace APE;

std::string U8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

FrameProgress::FrameProgress(IEncodeProgress* sink, ma_uint64 total)
    : m_sink(sink), m_total(total) {
    m_start = GetTickCount64();
}

void FrameProgress::update(ma_uint64 done) {
    if (m_total == kUnknownTotal) return; // indeterminate: stay quiet
    double frac = m_total ? (double)done / (double)m_total : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    double elapsed = (GetTickCount64() - m_start) / 1000.0;
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

bool FrameProgress::cancelled() {
    return m_sink && m_sink->Cancelled();
}

void FrameProgress::finish() {
    if (!m_sink) {
        if (m_total != kUnknownTotal) UI::EndProgress();
        else printf("\n");
    }
}

class MaPcmSource : public PcmSource {
public:
    MaPcmSource() { memset(&m_dec, 0, sizeof(m_dec)); }
    ~MaPcmSource() override {
        if (m_open) ma_decoder_uninit(&m_dec);
    }
    bool open(const std::wstring& path, std::wstring& err) override {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);
        if (ma_decoder_init_file_w(path.c_str(), &cfg, &m_dec) != MA_SUCCESS) {
            err = L"Cannot decode audio (unsupported or corrupt).";
            return false;
        }
        m_open = true;
        m_ch = (int)m_dec.outputChannels;
        m_rate = (int)m_dec.outputSampleRate;
        if (m_ch <= 0 || m_ch > 8 || m_rate <= 0) {
            err = L"Unsupported audio stream.";
            return false;
        }
        m_total = kUnknownTotal;
        ma_uint64 len = 0;
        if (ma_decoder_get_length_in_pcm_frames(&m_dec, &len) == MA_SUCCESS && len > 0)
            m_total = len;
        return true;
    }
    int channels() const override { return m_ch; }
    int sampleRate() const override { return m_rate; }
    ma_uint64 totalFrames() const override { return m_total; }
    bool seekBlock(int64_t block) override {
        if (block < 0) block = 0;
        return ma_decoder_seek_to_pcm_frame(&m_dec, (ma_uint64)block) == MA_SUCCESS;
    }
    int64_t readS16(int16_t* dst, uint64_t wantFrames) override {
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&m_dec, dst, wantFrames, &got);
        if (r != MA_SUCCESS && r != MA_AT_END) return -1;
        return (int64_t)got;
    }

private:
    ma_decoder m_dec;
    bool m_open = false;
    int m_ch = 0, m_rate = 0;
    ma_uint64 m_total = kUnknownTotal;
};

class ApePcmSource : public PcmSource {
public:
    ~ApePcmSource() override { delete m_dec; }
    bool open(const std::wstring& path, std::wstring& err) override {
        int code = 0;
        m_dec = CreateIAPEDecompress(path.c_str(), &code, false, true, false);
        if (!m_dec) {
            wchar_t b[128];
            swprintf_s(b, L"Not a valid .ape file (error %d).", code);
            err = b;
            return false;
        }
        m_rate = (int)m_dec->GetInfo(IAPEDecompress::APE_INFO_SAMPLE_RATE);
        m_ch = (int)m_dec->GetInfo(IAPEDecompress::APE_INFO_CHANNELS);
        m_total = (ma_uint64)m_dec->GetInfo(IAPEDecompress::APE_DECOMPRESS_TOTAL_BLOCKS);
        m_bits = (int)m_dec->GetInfo(IAPEDecompress::APE_INFO_BITS_PER_SAMPLE);
        int64_t flags = m_dec->GetInfo(IAPEDecompress::APE_INFO_FORMAT_FLAGS);
        m_isFloat = (flags & APE_FORMAT_FLAG_FLOATING_POINT) != 0;
        m_align = (int)m_dec->GetInfo(IAPEDecompress::APE_INFO_BLOCK_ALIGN);
        if (m_rate <= 0 || m_ch <= 0 || m_ch > 8 || m_total == 0 || m_align <= 0 ||
            (!m_isFloat && m_bits != 8 && m_bits != 16 && m_bits != 24 && m_bits != 32) ||
            (m_isFloat && m_bits != 32 && m_bits != 64)) {
            err = L"Unsupported APE stream.";
            return false;
        }
        m_raw.resize((size_t)4096 * (size_t)m_align);
        return true;
    }
    int channels() const override { return m_ch; }
    int sampleRate() const override { return m_rate; }
    ma_uint64 totalFrames() const override { return m_total; }
    bool depthReduced() const override { return !m_isFloat && m_bits > 16; }
    bool seekBlock(int64_t block) override {
        if (block < 0) block = 0;
        return m_dec->Seek(block) == 0;
    }
    int64_t readS16(int16_t* dst, uint64_t wantFrames) override {
        uint64_t done = 0;
        while (done < wantFrames) {
            uint64_t chunk = wantFrames - done;
            if (chunk > 4096) chunk = 4096;
            int64_t got = 0;
            if (m_dec->GetData(m_raw.data(), (int64_t)chunk, &got) != 0 || got < 0)
                return done > 0 ? (int64_t)done : -1;
            if (got == 0) break; // EOF
            Convert(m_raw.data(), (size_t)got, dst + done * (uint64_t)m_ch);
            done += (uint64_t)got;
        }
        return (int64_t)done;
    }

private:
    void Convert(const unsigned char* src, size_t frames, int16_t* dst) {
        size_t total = frames * (size_t)m_ch;
        if (!m_isFloat && m_bits == 16) {
            memcpy(dst, src, total * 2);
        } else if (!m_isFloat && m_bits == 8) {
            for (size_t i = 0; i < total; i++) dst[i] = (int16_t)(((int)src[i] - 128) << 8);
        } else if (!m_isFloat && m_bits == 24) {
            for (size_t i = 0; i < total; i++)
                dst[i] = (int16_t)(src[i * 3 + 1] | (src[i * 3 + 2] << 8));
        } else if (!m_isFloat && m_bits == 32) {
            for (size_t i = 0; i < total; i++)
                dst[i] = (int16_t)(src[i * 4 + 2] | (src[i * 4 + 3] << 8));
        } else if (m_isFloat && m_bits == 32) {
            const float* f = (const float*)src;
            for (size_t i = 0; i < total; i++) {
                float v = f[i] * 32767.0f;
                if (v > 32767.0f) v = 32767.0f;
                if (v < -32768.0f) v = -32768.0f;
                dst[i] = (int16_t)v;
            }
        } else { // float64
            const double* f = (const double*)src;
            for (size_t i = 0; i < total; i++) {
                double v = f[i] * 32767.0;
                if (v > 32767.0) v = 32767.0;
                if (v < -32768.0) v = -32768.0;
                dst[i] = (int16_t)v;
            }
        }
    }
    IAPEDecompress* m_dec = nullptr;
    int m_ch = 0, m_rate = 0, m_bits = 0, m_align = 0;
    bool m_isFloat = false;
    ma_uint64 m_total = 0;
    std::vector<unsigned char> m_raw;
};

bool PcmSource::seekBlock(int64_t block) {
    (void)block;
    return false; // no random access by default
}

std::unique_ptr<PcmSource> CreatePcmSource(const std::wstring& path) {
    std::wstring ext = FileExtension(path);
    if (ext == L".ape") return std::unique_ptr<PcmSource>(new ApePcmSource());
    if (ext == L".opus") return CreateOpusSource(); // encode_extra.cpp (unopened)
    if (ext == L".ogg" || ext == L".oga") return CreateOggSource(); // encode_extra.cpp
    if (ext == L".wav" || ext == L".mp3" || ext == L".flac")
        return std::unique_ptr<PcmSource>(new MaPcmSource());
    return nullptr;
}

std::unique_ptr<PcmSource> OpenPcmSource(const std::wstring& path, std::wstring& err) {
    std::unique_ptr<PcmSource> src = CreatePcmSource(path);
    if (!src) {
        err = L"Unsupported input type.";
        return nullptr;
    }
    if (!src->open(path, err)) return nullptr;
    return src;
}

DownmixSource::DownmixSource(std::unique_ptr<PcmSource> inner) : m_inner(std::move(inner)) {}

bool DownmixSource::open(const std::wstring& path, std::wstring& err) {
    if (!m_inner->open(path, err)) return false;
    if (m_inner->channels() <= 2) {
        err = L"Downmix of <=2ch source.";
        return false;
    }
    m_buf.resize((size_t)4096 * (size_t)m_inner->channels());
    return true;
}

int DownmixSource::sampleRate() const {
    return m_inner->sampleRate();
}

ma_uint64 DownmixSource::totalFrames() const {
    return m_inner->totalFrames();
}

bool DownmixSource::depthReduced() const {
    return m_inner->depthReduced();
}

bool DownmixSource::seekBlock(int64_t block) {
    return m_inner->seekBlock(block); // same frame count, fewer channels
}

int64_t DownmixSource::readS16(int16_t* dst, uint64_t wantFrames) {
    int inch = m_inner->channels();
    uint64_t done = 0;
    while (done < wantFrames) {
        uint64_t chunk = wantFrames - done;
        if (chunk > 4096) chunk = 4096;
        int64_t got = m_inner->readS16(m_buf.data(), chunk);
        if (got < 0) return done > 0 ? (int64_t)done : -1;
        if (got == 0) break;
        for (int64_t f = 0; f < got; f++) {
            int32_t l = 0, r = 0;
            int nl = 0, nr = 0;
            for (int c = 0; c < inch; c++) {
                int32_t v = m_buf[(size_t)f * (size_t)inch + (size_t)c];
                if ((c & 1) == 0) { l += v; nl++; } else { r += v; nr++; }
            }
            l = nl ? l / nl : 0;
            r = nr ? r / nr : l; // mono-ish content goes to both
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            dst[done * 2] = (int16_t)l;
            dst[done * 2 + 1] = (int16_t)r;
            done++;
        }
    }
    return (int64_t)done;
}

void DownmixSource::extraTags(const std::wstring& path,
                              std::vector<std::pair<std::wstring, std::wstring>>& fields) const {
    m_inner->extraTags(path, fields);
}

ResampleSource::ResampleSource(std::unique_ptr<PcmSource> inner, int targetRate)
    : m_inner(std::move(inner)), m_target(targetRate) {
    memset(&m_conv, 0, sizeof(m_conv));
}

ResampleSource::~ResampleSource() {
    if (m_convInit) ma_data_converter_uninit(&m_conv, NULL);
}

bool ResampleSource::open(const std::wstring& path, std::wstring& err) {
    if (!m_inner->open(path, err)) return false;
    ma_data_converter_config cfg = ma_data_converter_config_init(
        ma_format_s16, ma_format_s16, (ma_uint32)m_inner->channels(),
        (ma_uint32)m_inner->channels(), (ma_uint32)m_inner->sampleRate(), (ma_uint32)m_target);
    if (ma_data_converter_init(&cfg, NULL, &m_conv) != MA_SUCCESS) {
        err = L"Cannot set up resampler.";
        return false;
    }
    m_convInit = true;
    m_carry.resize((size_t)8192 * (size_t)m_inner->channels());
    m_out.resize((size_t)4096 * (size_t)m_inner->channels());
    m_carryOff = 0;
    m_carryLen = 0;
    ma_uint64 inTotal = m_inner->totalFrames();
    if (inTotal != kUnknownTotal) {
        ma_uint64 expected = 0;
        if (ma_data_converter_get_expected_output_frame_count(&m_conv, inTotal, &expected) ==
                MA_SUCCESS &&
            expected > 0)
            m_outTotal = expected;
    }
    return true;
}

int ResampleSource::channels() const {
    return m_inner->channels();
}

bool ResampleSource::depthReduced() const {
    return m_inner->depthReduced();
}

bool ResampleSource::seekBlock(int64_t block) {
    if (block < 0) block = 0;
    // Map to the inner rate, then rebuild the converter so no stale
    // filter state leaks across the seek.
    ma_uint64 inTotal = m_inner->totalFrames();
    ma_uint64 innerBlock =
        m_outTotal != kUnknownTotal && m_outTotal > 0
            ? (ma_uint64)((double)block * (double)inTotal / (double)m_outTotal)
            : (ma_uint64)((double)block * (double)m_inner->sampleRate() / (double)m_target);
    if (!m_inner->seekBlock((int64_t)innerBlock)) return false;
    if (m_convInit) ma_data_converter_uninit(&m_conv, NULL);
    ma_data_converter_config cfg = ma_data_converter_config_init(
        ma_format_s16, ma_format_s16, (ma_uint32)m_inner->channels(),
        (ma_uint32)m_inner->channels(), (ma_uint32)m_inner->sampleRate(), (ma_uint32)m_target);
    if (ma_data_converter_init(&cfg, NULL, &m_conv) != MA_SUCCESS) {
        m_convInit = false;
        return false;
    }
    m_convInit = true;
    m_carryOff = 0;
    m_carryLen = 0;
    m_innerEof = false;
    m_served = (ma_uint64)(block < 0 ? 0 : block);
    return true;
}

int64_t ResampleSource::pullConverted(int16_t* dst, uint64_t wantFrames) {
    // Pulls converted frames WITHOUT drop/total accounting (single place).
    // Unconsumed input is carried across calls - dropping it would eat the
    // stream (the converter takes only what it needs per call).
    uint64_t done = 0;
    int ch = m_inner->channels();
    if (m_out.size() < (size_t)4096 * (size_t)ch)
        m_out.resize((size_t)4096 * (size_t)ch); // paranoia: channels can't change, but be safe
    int16_t* out = m_out.data();
    while (done < wantFrames) {
        if (m_carryLen == 0 && !m_innerEof) {
            int64_t got = m_inner->readS16(m_carry.data(), 8192);
            if (got < 0) return done > 0 ? (int64_t)done : -1;
            if (got == 0) {
                m_innerEof = true;
            } else {
                m_carryOff = 0;
                m_carryLen = (size_t)got;
            }
        }
        uint64_t chunk = wantFrames - done;
        if (chunk > 4096) chunk = 4096;
        ma_uint64 inAvail = m_carryLen;
        const void* inPtr = m_carryLen > 0 ? (const void*)(m_carry.data() + m_carryOff * (size_t)ch)
                                           : nullptr;
        if (inAvail == 0 && m_innerEof) {
            // Fully drained: converter has no more cached output worth
            // chasing for file-length accuracy; the accounted total governs.
            break;
        }
        ma_uint64 inUsed = inAvail, outGot = chunk;
        if (ma_data_converter_process_pcm_frames(&m_conv, inPtr, &inUsed,
                                                 out, &outGot) != MA_SUCCESS)
            return done > 0 ? (int64_t)done : -1;
        if (inUsed > 0) {
            if (inUsed > m_carryLen) inUsed = m_carryLen;
            m_carryOff += (size_t)inUsed;
            m_carryLen -= (size_t)inUsed;
            if (m_carryLen == 0) m_carryOff = 0;
        }
        if (outGot == 0) {
            if (m_innerEof && inAvail == 0) break;
            if (inAvail > 0 && inUsed == 0) break; // stuck converter: stop, don't spin
            continue; // converter needs more input than one chunk gives
        }
        memcpy(dst + done * (uint64_t)ch, out, (size_t)outGot * (size_t)ch * 2);
        done += outGot;
    }
    return (int64_t)done;
}

int64_t ResampleSource::readS16(int16_t* dst, uint64_t wantFrames) {
    if (m_outTotal == kUnknownTotal || m_outTotal == 0) return pullConverted(dst, wantFrames);
    // Serve exactly m_outTotal frames from the start. The converter's
    // priming transient (a few samples) is inaudible; dropping the full
    // reported output latency instead risks eating the whole stream, so
    // duration stays exact this way.
    if (m_served >= m_outTotal) return 0;
    uint64_t left = m_outTotal - m_served;
    if (wantFrames > left) wantFrames = left;
    int64_t got = pullConverted(dst, wantFrames);
    if (got > 0) m_served += (ma_uint64)got;
    return got;
}

void ResampleSource::extraTags(const std::wstring& path,
                               std::vector<std::pair<std::wstring, std::wstring>>& fields) const {
    m_inner->extraTags(path, fields);
}
