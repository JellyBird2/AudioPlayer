// AudioPlayer - decode (any supported format) + play (miniaudio/WASAPI).
// .ape goes through the MAC SDK (bit-exact, all depths); everything else
// streams 16-bit PCM through the shared PcmSource pipeline.
// UI-agnostic: used by the GUIs and the CLI. Not thread-safe except where
// noted; the audio callback runs on miniaudio's thread and only touches
// atomics + the decoder under mutex.
#pragma once

#include "ma_config.h"
#include "miniaudio.h"

#include "soundtouch/SoundTouch.h"

#include "All.h"
#include "MACLib.h"
#include "IAPETag.h"

#include "encode_pcm.h"

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>

struct CiLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

struct AudioTrackInfo {
    std::wstring path;
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring formatName; // L"APE", L"MP3", L"FLAC", ...
    int sampleRate = 0;
    int bits = 0;
    int channels = 0;
    int64_t totalBlocks = 0; // 0 when the length is not known up front
    int level = 0;           // APE compression level, else 0
    int64_t avgBitrate = 0;  // kbps when known, else 0
    int64_t apeBytes = 0;    // source file size in bytes
    int64_t fileVersion = 0; // APE file version, else 0
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // Opens a file (closes any previous). False + err on failure.
    bool Open(const wchar_t* path, std::wstring& err);
    void Close();
    bool IsOpen() const { return m_dec != nullptr || m_src != nullptr; }

    // Starts audio output. False + err if the device fails.
    bool Start(std::wstring& err);
    // Stops audio output, keeps the file open.
    void Stop();
    bool DeviceRunning() const { return m_devRunning; }
    bool Finished() const { return m_finished.load(); }

    bool Paused() const { return m_paused.load(); }
    void SetPaused(bool p) { m_paused = p; }

    void SeekTo(int64_t block);
    int64_t Position() const { return m_blocksPlayed.load(); }
    // Restarts from the beginning (also clears the finished flag).
    void Replay();

    void SetVolume(float v);
    float Volume() const { return m_volume.load(); }
    void SetMuted(bool m) { m_muted = m; }
    bool Muted() const { return m_muted.load(); }

    // Playback speed: 1.0 = normal, no upper limit. Must be finite and
    // > 0 (anything else is ignored). Applies live, persists across tracks
    // until changed. Pitch-preserving (SoundTouch) unless
    // SetPreservePitch(false) selects the tape-style resample.
    void SetSpeed(float s);
    float Speed() const { return m_speed.load(); }
    void SetPreservePitch(bool p);
    bool PreservePitch() const { return m_preservePitch.load(); }

    const AudioTrackInfo& Info() const { return m_info; }
    // Reads any tag field (Title/Artist/Album/Genre/Year/Track/Comment...).
    // APE tags for .ape, native tags for the rest. Empty when absent.
    std::wstring TagField(const wchar_t* name);
    // Embedded cover art as pure image bytes (header stripped). APE only;
    // empty when absent.
    std::vector<unsigned char> CoverImage();

private:
    static void AudioCB(ma_device* dev, void* out, const void* in, ma_uint32 frames);
    bool OpenApe(const wchar_t* path, std::wstring& err);
    bool OpenStream(const wchar_t* path, std::wstring& err);
    // (Re)configures the stretcher for the current file. Call with no
    // device running (Open paths) or with m_mtx held (audio thread).
    void SetupStretchLocked();
    // Decodes through the stretcher into out (native device format).
    // Returns output frames produced; advances m_blocksPlayed by the
    // SOURCE frames consumed so seek/duration stay speed-independent.
    // Call with m_mtx held.
    int64_t ReadStretchedLocked(void* out, ma_uint32 frameCount);
    // Raw decode helper (native format, no gain/stretch). Call with m_mtx held.
    int64_t DecodeRawLocked(unsigned char* dst, int64_t wantFrames);

    APE::IAPEDecompress* m_dec = nullptr;
    std::unique_ptr<PcmSource> m_src;
    std::map<std::wstring, std::wstring, CiLess> m_tags;
    std::mutex m_mtx;
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_finished{false};
    std::atomic<int64_t> m_blocksPlayed{0};
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<float> m_speed{1.0f};
    std::atomic<bool> m_preservePitch{true};

    // Time-stretch state. m_st itself is only touched on the audio thread
    // (inside AudioCB under m_mtx) and in the Open paths (no device
    // running); the atomics above carry settings across threads, with
    // m_stDirty requesting re-apply + clear.
    soundtouch::SoundTouch m_st;
    bool m_stReady = false;  // configured for the current file
    bool m_stDirty = true;   // speed/pitch changed: re-apply + clear
    bool m_stEofFed = false; // decoder EOF seen: flush() issued, draining
    std::vector<unsigned char> m_stRaw; // reused native-format decode scratch
    std::vector<float> m_stFlt;         // reused float conversion scratch
    std::vector<float> m_stOut;         // reused float output scratch

    ma_device m_dev;
    bool m_devRunning = false;
    int m_blockAlign = 0;
    ma_format m_fmt = ma_format_s16;
    int m_channels = 0;
    AudioTrackInfo m_info;
};
