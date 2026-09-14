// AudioPlayer implementation. See win_player.h.
#define MINIAUDIO_IMPLEMENTATION
#include "win_player.h"

#include "CharacterHelper.h"
#include "encode_extra.h"
#include "id3.h"

#include <stdio.h>
#include <stdint.h>
#include <cstring>
#include <cmath>
#include <cfloat>

using namespace APE;

namespace {

std::wstring TagStr(IAPETag* tag, const wchar_t* name) {
    if (!tag) return std::wstring();
    wchar_t buf[1024];
    int chars = 1024;
    if (tag->GetFieldString(name, buf, &chars) == 0 && chars > 0 && buf[0] != 0)
        return std::wstring(buf);
    return std::wstring();
}

std::wstring LowerExt(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/:");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep))
        return std::wstring();
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

std::wstring FormatNameForExt(const std::wstring& ext) {
    if (ext == L".ape") return L"APE";
    if (ext == L".wav") return L"WAV";
    if (ext == L".mp3") return L"MP3";
    if (ext == L".flac") return L"FLAC";
    if (ext == L".ogg" || ext == L".oga") return L"Ogg Vorbis";
    if (ext == L".opus") return L"Opus";
    return L"Audio";
}

unsigned long long FileSizeOf(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

void ApplyGain(void* out, ma_uint32 frames, ma_format fmt, int channels, float vol) {
    if (vol == 1.0f) return;
    if (vol < 0) vol = 0;
    size_t total = (size_t)frames * (size_t)channels;
    if (vol == 0.0f) {
        // Muted: silence with memset instead of per-sample multiply.
        size_t bytesPerSample = (fmt == ma_format_s24) ? 3 : (fmt == ma_format_s16) ? 2 : 4;
        memset(out, 0, total * bytesPerSample);
        return;
    }
    if (fmt == ma_format_f32) {
        float* p = (float*)out;
        for (size_t i = 0; i < total; i++) p[i] *= vol;
    } else if (fmt == ma_format_s16) {
        int16_t* p = (int16_t*)out;
        for (size_t i = 0; i < total; i++) {
            int32_t v = (int32_t)(p[i] * vol);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            p[i] = (int16_t)v;
        }
    } else if (fmt == ma_format_s32) {
        int32_t* p = (int32_t*)out;
        for (size_t i = 0; i < total; i++) {
            double v = (double)p[i] * (double)vol;
            if (v > 2147483647.0) v = 2147483647.0;
            if (v < -2147483648.0) v = -2147483648.0;
            p[i] = (int32_t)v;
        }
    } else if (fmt == ma_format_s24) {
        uint8_t* b = (uint8_t*)out;
        for (size_t i = 0; i < total; i++) {
            int32_t v = (int32_t)(b[0] | (b[1] << 8) | ((int8_t)b[2] << 16));
            v = (int32_t)(v * vol);
            if (v > 8388607) v = 8388607;
            if (v < -8388608) v = -8388608;
            b[0] = (uint8_t)(v & 0xFF);
            b[1] = (uint8_t)((v >> 8) & 0xFF);
            b[2] = (uint8_t)((v >> 16) & 0xFF);
            b += 3;
        }
    }
}

// Native device samples <-> float (for the time-stretcher). total = samples.
void ToFloat(const void* src, float* dst, size_t total, ma_format fmt) {
    if (fmt == ma_format_f32) {
        memcpy(dst, src, total * 4);
    } else if (fmt == ma_format_s16) {
        const int16_t* p = (const int16_t*)src;
        for (size_t i = 0; i < total; i++) dst[i] = (float)p[i] / 32768.0f;
    } else if (fmt == ma_format_s32) {
        const int32_t* p = (const int32_t*)src;
        for (size_t i = 0; i < total; i++) dst[i] = (float)((double)p[i] / 2147483648.0);
    } else if (fmt == ma_format_s24) {
        const uint8_t* b = (const uint8_t*)src;
        for (size_t i = 0; i < total; i++, b += 3) {
            int32_t v = (int32_t)(b[0] | (b[1] << 8) | ((int8_t)b[2] << 16));
            dst[i] = (float)v / 8388608.0f;
        }
    }
}

void FromFloat(const float* src, void* dst, size_t total, ma_format fmt) {
    if (total == 0) return;
    if (fmt == ma_format_f32) {
        memcpy(dst, src, total * 4);
    } else if (fmt == ma_format_s16) {
        int16_t* p = (int16_t*)dst;
        for (size_t i = 0; i < total; i++) {
            float v = src[i];
            if (v >= 1.0f) p[i] = 32767;
            else if (v <= -1.0f) p[i] = -32768;
            else p[i] = (int16_t)lrintf(v * 32768.0f);
        }
    } else if (fmt == ma_format_s32) {
        int32_t* p = (int32_t*)dst;
        for (size_t i = 0; i < total; i++) {
            double v = (double)src[i] * 2147483648.0;
            if (v > 2147483647.0) v = 2147483647.0;
            if (v < -2147483648.0) v = -2147483648.0;
            p[i] = (int32_t)v;
        }
    } else if (fmt == ma_format_s24) {
        uint8_t* b = (uint8_t*)dst;
        for (size_t i = 0; i < total; i++, b += 3) {
            float v = src[i];
            int32_t s;
            if (v >= 1.0f) s = 8388607;
            else if (v <= -1.0f) s = -8388608;
            else s = (int32_t)lrintf(v * 8388608.0f);
            b[0] = (uint8_t)(s & 0xFF);
            b[1] = (uint8_t)((s >> 8) & 0xFF);
            b[2] = (uint8_t)((s >> 16) & 0xFF);
        }
    }
}

} // namespace

AudioPlayer::AudioPlayer() {
    memset(&m_dev, 0, sizeof(m_dev));
}

AudioPlayer::~AudioPlayer() {
    Close();
}

bool AudioPlayer::Open(const wchar_t* path, std::wstring& err) {
    Close();
    std::wstring ext = LowerExt(path);
    if (ext == L".ape") return OpenApe(path, err);
    if (ext == L".wav" || ext == L".mp3" || ext == L".flac" || ext == L".ogg" ||
        ext == L".oga" || ext == L".opus")
        return OpenStream(path, err);
    err = L"Unsupported audio type.";
    return false;
}

bool AudioPlayer::OpenApe(const wchar_t* path, std::wstring& err) {
    int code = 0;
    IAPEDecompress* dec = CreateIAPEDecompress(path, &code, false, true, false);
    if (!dec) {
        wchar_t b[256];
        swprintf_s(b, L"Not a valid .ape file (error %d).", code);
        err = b;
        return false;
    }

    int64_t sampleRate = dec->GetInfo(IAPEDecompress::APE_INFO_SAMPLE_RATE);
    int64_t bits = dec->GetInfo(IAPEDecompress::APE_INFO_BITS_PER_SAMPLE);
    int64_t ch = dec->GetInfo(IAPEDecompress::APE_INFO_CHANNELS);
    int64_t totalBlocks = dec->GetInfo(IAPEDecompress::APE_DECOMPRESS_TOTAL_BLOCKS);
    int64_t blockAlign = dec->GetInfo(IAPEDecompress::APE_INFO_BLOCK_ALIGN);
    int64_t flags = dec->GetInfo(IAPEDecompress::APE_INFO_FORMAT_FLAGS);
    int64_t level = dec->GetInfo(IAPEDecompress::APE_INFO_COMPRESSION_LEVEL);

    if (sampleRate <= 0 || ch <= 0 || ch > 8 || totalBlocks <= 0 || blockAlign <= 0) {
        err = L"Unsupported or corrupt APE stream.";
        delete dec;
        return false;
    }

    ma_format fmt = ma_format_s16;
    bool isFloat = (flags & APE_FORMAT_FLAG_FLOATING_POINT) != 0;
    if (isFloat) fmt = ma_format_f32;
    else if (bits == 8) fmt = ma_format_s16;
    else if (bits == 16) fmt = ma_format_s16;
    else if (bits == 24) fmt = ma_format_s24;
    else if (bits == 32) fmt = ma_format_s32;
    else {
        err = L"Unsupported bits per sample.";
        delete dec;
        return false;
    }
    if (bits == 8) blockAlign = ch * 2;

    m_dec = dec;
    m_fmt = fmt;
    m_channels = (int)ch;
    m_blockAlign = (int)blockAlign;
    m_info.path = path;
    m_info.title = TagStr(dec->GetTag(), L"Title");
    m_info.artist = TagStr(dec->GetTag(), L"Artist");
    m_info.album = TagStr(dec->GetTag(), L"Album");
    m_info.formatName = L"APE";
    m_info.sampleRate = (int)sampleRate;
    m_info.bits = (int)bits;
    m_info.channels = (int)ch;
    m_info.totalBlocks = totalBlocks;
    m_info.level = (int)level;
    m_info.avgBitrate = dec->GetInfo(IAPEDecompress::APE_DECOMPRESS_AVERAGE_BITRATE);
    m_info.apeBytes = dec->GetInfo(IAPEDecompress::APE_INFO_APE_TOTAL_BYTES);
    m_info.fileVersion = dec->GetInfo(IAPEDecompress::APE_INFO_FILE_VERSION);
    SetupStretchLocked();
    return true;
}

bool AudioPlayer::OpenStream(const wchar_t* path, std::wstring& err) {
    std::unique_ptr<PcmSource> src = OpenPcmSource(path, err);
    if (!src) return false;
    int ch = src->channels(), rate = src->sampleRate();
    if (ch <= 0 || ch > 8 || rate <= 0) {
        err = L"Unsupported audio stream.";
        return false;
    }
    ma_uint64 total = src->totalFrames();

    // Native tags into the lookup map.
    std::wstring ext = LowerExt(path);
    std::vector<std::pair<std::wstring, std::wstring>> fields;
    if (ext == L".mp3") {
        ID3Tags id3;
        if (ReadID3Tags(path, id3) && !id3.empty()) {
            auto m = id3.fields();
            for (const auto& kv : m) fields.push_back(kv);
        }
    } else if (ext == L".ogg" || ext == L".oga") {
        ReadVorbisFileTags(path, fields);
    } else if (ext == L".flac") {
        ReadFlacTags(path, fields);
    } else if (ext == L".wav") {
        ReadWavTags(path, fields);
    } else {
        src->extraTags(path, fields); // e.g. Opus comments
    }
    m_tags.clear();
    for (const auto& kv : fields) m_tags[kv.first] = kv.second;
    auto tag = [&](const wchar_t* n) {
        auto it = m_tags.find(n);
        return it == m_tags.end() ? std::wstring() : it->second;
    };

    m_src = std::move(src);
    m_fmt = ma_format_s16;
    m_channels = ch;
    m_blockAlign = ch * 2;
    m_info.path = path;
    m_info.title = tag(L"Title");
    m_info.artist = tag(L"Artist");
    m_info.album = tag(L"Album");
    m_info.formatName = FormatNameForExt(ext);
    m_info.sampleRate = rate;
    m_info.bits = 16;
    m_info.channels = ch;
    m_info.totalBlocks = total == kUnknownTotal ? 0 : (int64_t)total;
    m_info.level = 0;
    m_info.avgBitrate = 0;
    m_info.apeBytes = (int64_t)FileSizeOf(path);
    m_info.fileVersion = 0;
    SetupStretchLocked();
    return true;
}

void AudioPlayer::Close() {
    Stop();
    if (m_dec) {
        delete m_dec;
        m_dec = nullptr;
    }
    m_src.reset();
    m_tags.clear();
    m_stReady = false; // stretcher reconfigured on next Open
    m_blocksPlayed = 0;
    m_finished = false;
    m_paused = false;
    m_info = AudioTrackInfo();
}

bool AudioPlayer::Start(std::wstring& err) {
    if (!m_dec && !m_src) {
        err = L"No file open.";
        return false;
    }
    if (m_devRunning) return true;

    ma_device_config dcfg = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format = m_fmt;
    dcfg.playback.channels = (ma_uint32)m_channels;
    dcfg.sampleRate = (ma_uint32)m_info.sampleRate;
    dcfg.dataCallback = AudioCB;
    dcfg.pUserData = this;

    if (ma_device_init(nullptr, &dcfg, &m_dev) != MA_SUCCESS) {
        err = L"Failed to open audio device.";
        return false;
    }
    if (ma_device_start(&m_dev) != MA_SUCCESS) {
        ma_device_uninit(&m_dev);
        err = L"Failed to start audio device.";
        return false;
    }
    m_devRunning = true;
    return true;
}

void AudioPlayer::Stop() {
    if (m_devRunning) {
        ma_device_uninit(&m_dev);
        m_devRunning = false;
    }
}

void AudioPlayer::SeekTo(int64_t block) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (block < 0) block = 0;
    if (m_info.totalBlocks > 0 && block >= m_info.totalBlocks)
        block = m_info.totalBlocks - 1;
    if (m_dec) {
        if (m_dec->Seek(block) == 0) {
            m_blocksPlayed = block;
            m_st.clear(); // drop stretched audio from the old position
            m_stEofFed = false;
        }
    } else if (m_src) {
        if (m_src->seekBlock(block)) {
            m_blocksPlayed = block;
            m_st.clear();
            m_stEofFed = false;
        }
    }
}

void AudioPlayer::SetVolume(float v) {
    if (v < 0) v = 0;
    if (v > 1.5f) v = 1.5f;
    m_volume = v;
}

void AudioPlayer::SetSpeed(float s) {
    if (!(s > 0.0f) || !(s < FLT_MAX)) return; // NaN, inf, zero, negative: ignore
    m_speed = s;
    m_stDirty = true; // audio thread re-applies + clears under lock
}

void AudioPlayer::SetPreservePitch(bool p) {
    m_preservePitch = p;
    m_stDirty = true;
}

void AudioPlayer::SetupStretchLocked() {
    m_st.setSampleRate((unsigned int)m_info.sampleRate);
    m_st.setChannels((unsigned int)m_channels);
    float speed = m_speed.load();
    if (m_preservePitch.load()) {
        m_st.setTempo((double)speed); // tempo shifts, pitch stays
        m_st.setRate(1.0);
    } else {
        m_st.setTempo(1.0);
        m_st.setRate((double)speed); // tape-style: pitch follows tempo
    }
    m_st.clear(); // drop audio processed at the old setting
    m_stEofFed = false;
    m_stDirty = false;
    m_stReady = true;
}

int64_t AudioPlayer::DecodeRawLocked(unsigned char* dst, int64_t wantFrames) {
    if (m_dec) {
        int64_t got = 0;
        m_dec->GetData(dst, wantFrames, &got);
        return got;
    }
    if (m_src) return m_src->readS16((int16_t*)dst, (uint64_t)wantFrames);
    return -1;
}

int64_t AudioPlayer::ReadStretchedLocked(void* out, ma_uint32 frameCount) {
    const int ch = m_channels;
    const size_t kChunk = 2048; // source frames per decode step
    size_t needRaw = kChunk * (size_t)m_blockAlign;
    if (m_stRaw.size() < needRaw) m_stRaw.resize(needRaw);
    size_t needFlt = kChunk * (size_t)ch;
    if (m_stFlt.size() < needFlt) m_stFlt.resize(needFlt);
    size_t needOut = (size_t)frameCount * (size_t)ch;
    if (needOut > 0 && m_stOut.size() < needOut) m_stOut.resize(needOut);

    if (m_stDirty) SetupStretchLocked();

    ma_uint32 done = 0;
    for (int iter = 0; iter < 64 && done < frameCount; iter++) {
        if (!m_stEofFed && m_st.numSamples() == 0) {
            int64_t got = DecodeRawLocked(m_stRaw.data(), (int64_t)kChunk);
            if (got <= 0) {
                m_st.flush(); // EOF (or decode error): drain what remains
                m_stEofFed = true;
            } else {
                ToFloat(m_stRaw.data(), m_stFlt.data(), (size_t)got * (size_t)ch, m_fmt);
                m_st.putSamples(m_stFlt.data(), (unsigned int)got);
                m_blocksPlayed += got; // source frames: seek stays speed-independent
            }
        }
        unsigned int n =
            m_st.receiveSamples(m_stOut.data() + (size_t)done * (size_t)ch, frameCount - done);
        done += n;
        if (n == 0 && m_stEofFed) break; // fully drained
        // n == 0 with input left means the stretcher wants more input;
        // the loop decodes another chunk (or flushes at EOF above).
    }
    FromFloat(m_stOut.data(), out, (size_t)done * (size_t)ch, m_fmt);
    return (int64_t)done;
}

void AudioPlayer::AudioCB(ma_device* dev, void* out, const void* in, ma_uint32 frameCount) {
    (void)in;
    AudioPlayer* self = (AudioPlayer*)dev->pUserData;
    size_t bytesPerFrame = (size_t)self->m_blockAlign;
    if (self->m_paused.load() || self->m_finished.load()) {
        memset(out, 0, (size_t)frameCount * bytesPerFrame);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(self->m_mtx);
        if (self->m_finished.load()) {
            memset(out, 0, (size_t)frameCount * bytesPerFrame);
            return;
        }
        int64_t got = 0;
        if (self->m_stReady && self->m_speed.load() != 1.0f) {
            got = self->ReadStretchedLocked(out, frameCount);
            if (got < 0) got = 0;
            if ((ma_uint64)got < frameCount) {
                memset((char*)out + (size_t)got * bytesPerFrame, 0,
                       (size_t)((int64_t)frameCount - got) * bytesPerFrame);
            }
            // Only truly finished when the stretcher is drained; a short
            // read with input left just resumes on the next callback.
            if (got == 0 && self->m_stEofFed && self->m_st.numSamples() == 0)
                self->m_finished = true;
            // (m_blocksPlayed advanced inside by source frames consumed.)
        } else {
            if (self->m_dec) {
                int ret = self->m_dec->GetData((unsigned char*)out, (int64_t)frameCount, &got);
                (void)ret;
            } else if (self->m_src) {
                got = self->m_src->readS16((int16_t*)out, frameCount);
                if (got < 0) got = 0;
            }
            if (got < 0) got = 0;
            if ((ma_uint64)got < frameCount) {
                memset((char*)out + (size_t)got * bytesPerFrame, 0,
                       (size_t)((int64_t)frameCount - got) * bytesPerFrame);
            }
            if (got <= 0) self->m_finished = true;
            self->m_blocksPlayed += got;
        }
    }
    float vol = self->m_muted.load() ? 0.0f : self->m_volume.load();
    ApplyGain(out, frameCount, self->m_fmt, self->m_channels, vol);
}

void AudioPlayer::Replay() {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_dec) {
        if (m_dec->Seek(0) == 0) {
            m_blocksPlayed = 0;
            m_finished = false;
            m_st.clear();
            m_stEofFed = false;
        }
    } else if (m_src) {
        if (m_src->seekBlock(0)) {
            m_blocksPlayed = 0;
            m_finished = false;
            m_st.clear();
            m_stEofFed = false;
        }
    }
}

std::wstring AudioPlayer::TagField(const wchar_t* name) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_dec) return TagStr(m_dec->GetTag(), name);
    auto it = m_tags.find(name ? name : L"");
    return it == m_tags.end() ? std::wstring() : it->second;
}

namespace {

bool HasImageMagic(const unsigned char* p, size_t n) {
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return true; // JPEG
    if (n >= 8 && memcmp(p, "\x89PNG\r\n\x1A\n", 8) == 0) return true;      // PNG
    if (n >= 6 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0)) return true;
    if (n >= 2 && p[0] == 'B' && p[1] == 'M') return true;                  // BMP
    if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0) return true;
    if (n >= 4 && ((p[0] == 'I' && p[1] == 'I' && p[2] == 42 && p[3] == 0) ||
                   (p[0] == 'M' && p[1] == 'M' && p[2] == 0 && p[3] == 42)))
        return true; // TIFF
    return false;
}

} // namespace

std::vector<unsigned char> AudioPlayer::CoverImage() {
    std::lock_guard<std::mutex> lk(m_mtx);
    std::vector<unsigned char> out;
    if (!m_dec) return out;
    APE::IAPETag* tag = m_dec->GetTag();
    if (!tag) return out;

    // Probe for the field size first (missing field reports 0).
    int bytes = 1;
    unsigned char probe = 0;
    tag->GetFieldBinary(APE_TAG_FIELD_COVER_ART_FRONT, &probe, &bytes);
    if (bytes <= 1) return out;
    out.resize((size_t)bytes);
    int again = bytes;
    if (tag->GetFieldBinary(APE_TAG_FIELD_COVER_ART_FRONT, out.data(), &again) != 0) {
        out.clear();
        return out;
    }
    out.resize((size_t)again);

    // Field layout is [extension|filename] NUL image-data. Split at the first
    // NUL and validate; otherwise scan the blob for an image header.
    const unsigned char* base = out.data();
    size_t total = out.size();
    size_t start = total;
    for (size_t i = 0; i < total; i++) {
        if (base[i] == 0) { start = i + 1; break; }
    }
    if (start < total && HasImageMagic(base + start, total - start))
        return std::vector<unsigned char>(base + start, base + total);
    for (size_t i = 0; i + 4 < total; i++) {
        if (HasImageMagic(base + i, total - i))
            return std::vector<unsigned char>(base + i, base + total);
    }
    out.clear();
    return out;
}
