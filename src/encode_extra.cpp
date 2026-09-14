// Extra format engines (FLAC/Vorbis/Opus/MP3) + Opus input + tag
// readers/writers for non-APE formats. See encode_extra.h.
#include "encode_extra.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>
#include <FLAC/stream_encoder.h>
#include <FLAC/metadata.h>
#include <opus/opus.h>
#include <lame/lame.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

// ---------------------------------------------------------------- vorbis comments
// Internal names <-> Vorbis field names (also used for FLAC/Opus tags).
struct NameMap {
    const wchar_t* internal;
    const char* vorbis;
};
static const NameMap kNames[] = {
    {L"Title", "TITLE"}, {L"Artist", "ARTIST"}, {L"Album", "ALBUM"},
    {L"Genre", "GENRE"}, {L"Year", "DATE"}, {L"Track", "TRACKNUMBER"},
    {L"Comment", "DESCRIPTION"},
};

std::string ToVorbisName(const std::wstring& internal) {
    for (const auto& m : kNames)
        if (_wcsicmp(internal.c_str(), m.internal) == 0) return m.vorbis;
    std::string s = U8(internal.c_str());
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

std::wstring FromVorbisName(const char* name) {
    std::string up = name ? name : "";
    for (auto& c : up) c = (char)toupper((unsigned char)c);
    for (const auto& m : kNames)
        if (up == m.vorbis) return m.internal;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    if (n > 0) {
        std::wstring w((size_t)(n - 1), 0);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, w.data(), n);
        return w;
    }
    std::wstring w;
    for (const char* p = name; *p; p++) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

std::wstring WidenTagBytes(const char* p, size_t n) {
    if (n == 0) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, (int)n, nullptr, 0);
    if (need > 0) {
        std::wstring w((size_t)need, 0);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, (int)n, w.data(), need);
        while (!w.empty() && (w.back() == 0 || w.back() == L' ')) w.pop_back();
        return w;
    }
    std::wstring w;
    w.reserve(n);
    for (size_t i = 0; i < n; i++) {
        unsigned char b = (unsigned char)p[i];
        if (b == 0) break;
        w.push_back((wchar_t)b);
    }
    while (!w.empty() && w.back() == L' ') w.pop_back();
    return w;
}

// Splits one "NAME=value" comment; appends (Internal, value). False if malformed.
bool SplitComment(const char* data, size_t len,
                  std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    size_t eq = 0;
    while (eq < len && data[eq] != '=') eq++;
    if (eq == 0 || eq >= len) return false;
    std::string name(data, eq);
    fields.push_back({FromVorbisName(name.c_str()), WidenTagBytes(data + eq + 1, len - eq - 1)});
    return true;
}

// ---------------------------------------------------------------- ogg file sink
struct OggFileWriter {
    FILE* f = nullptr;
    ogg_stream_state os;
    bool open = false;
    bool begin(FILE* f_, int serial) {
        f = f_;
        if (ogg_stream_init(&os, serial) != 0) return false;
        open = true;
        return true;
    }
    bool writePage(ogg_page* pg) {
        if (fwrite(pg->header, 1, (size_t)pg->header_len, f) != (size_t)pg->header_len)
            return false;
        if (fwrite(pg->body, 1, (size_t)pg->body_len, f) != (size_t)pg->body_len) return false;
        return !ferror(f);
    }
    // Queue a packet; flush=true forces pages out (headers, EOS).
    bool packet(ogg_packet* op, bool flush = false) {
        if (ogg_stream_packetin(&os, op) != 0) return false;
        ogg_page pg;
        while (flush ? ogg_stream_flush(&os, &pg) : ogg_stream_pageout(&os, &pg)) {
            if (!writePage(&pg)) return false;
        }
        return true;
    }
    void end() {
        if (open) {
            ogg_stream_clear(&os);
            open = false;
        }
    }
};

static uint32_t Le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool SameInternal(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

} // namespace

int NearestMp3Bitrate(int want) {
    static const int kTable[] = {8,   16,  24,  32,  40,  48,  56,  64,  80,  96,  112,
                                 128, 160, 192, 224, 256, 320};
    int best = 192;
    for (int b : kTable) {
        if (abs(b - want) < abs(best - want)) best = b;
    }
    return best;
}

bool LameRateOk(int rate) {
    static const int kRates[] = {8000, 11025, 12000, 16000,  22050, 24000,
                                 32000, 44100, 48000};
    for (int r : kRates)
        if (r == rate) return true;
    return false;
}

// ---------------------------------------------------------------- FLAC out
int EncodeFlacFromPcm(PcmSource& src, const std::wstring& out, int flacLevel,
                      const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                      IEncodeProgress* sink) {
    if (flacLevel < 0) flacLevel = 0;
    if (flacLevel > 8) flacLevel = 8;
    int ch = src.channels(), rate = src.sampleRate();
    ma_uint64 total = src.totalFrames();

    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f) {
        if (!sink) fwprintf(stderr, L"Cannot create output: %ls\n", out.c_str());
        return 4;
    }
    FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
    if (!enc) {
        fclose(f);
        DeleteFileW(out.c_str());
        return 4;
    }
    FLAC__stream_encoder_set_channels(enc, (unsigned)ch);
    FLAC__stream_encoder_set_bits_per_sample(enc, 16);
    FLAC__stream_encoder_set_sample_rate(enc, (unsigned)rate);
    FLAC__stream_encoder_set_compression_level(enc, (unsigned)flacLevel);
    if (total != kUnknownTotal)
        FLAC__stream_encoder_set_total_samples_estimate(enc, total);

    FLAC__StreamMetadata* md = nullptr;
    if (!fields.empty()) {
        md = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
        if (md) {
            FLAC__StreamMetadata_VorbisComment_Entry vendor;
            vendor.entry = (FLAC__byte*)"AudioPlayer";
            vendor.length = 10;
            FLAC__metadata_object_vorbiscomment_set_vendor_string(md, vendor, true);
            for (const auto& kv : fields) {
                FLAC__StreamMetadata_VorbisComment_Entry e;
                if (FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
                        &e, ToVorbisName(kv.first).c_str(), U8(kv.second.c_str()).c_str()))
                    FLAC__metadata_object_vorbiscomment_append_comment(md, e, false);
            }
            FLAC__StreamMetadata* mds[1] = {md};
            FLAC__stream_encoder_set_metadata(enc, mds, 1);
        }
    }

    int rc = 4;
    if (FLAC__stream_encoder_init_FILE(enc, f, nullptr, nullptr) ==
        FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        FrameProgress prog(sink, total);
        std::vector<int16_t> pcm((size_t)4096 * (size_t)ch);
        std::vector<FLAC__int32> s32((size_t)4096 * (size_t)ch);
        ma_uint64 fed = 0;
        rc = 0;
        for (;;) {
            if (prog.cancelled()) { rc = 5; break; }
            int64_t got = src.readS16(pcm.data(), 4096);
            if (got < 0) { rc = 4; break; }
            if (got == 0) break;
            for (int64_t i = 0; i < got * ch; i++) s32[(size_t)i] = pcm[(size_t)i];
            if (!FLAC__stream_encoder_process_interleaved(enc, s32.data(), (uint32_t)got)) {
                rc = 4;
                break;
            }
            fed += (ma_uint64)got;
            prog.update(fed);
        }
        prog.finish();
        if (rc == 0 && !FLAC__stream_encoder_finish(enc)) rc = 4;
    }
    FLAC__stream_encoder_delete(enc);
    if (md) FLAC__metadata_object_delete(md);
    fclose(f);
    if (rc != 0) DeleteFileW(out.c_str());
    return rc;
}

// ---------------------------------------------------------------- Ogg Vorbis out
int EncodeVorbisFromPcm(PcmSource& src, const std::wstring& out, float quality,
                        const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                        IEncodeProgress* sink) {
    if (quality < -0.1f) quality = -0.1f;
    if (quality > 1.0f) quality = 1.0f;
    int ch = src.channels(), rate = src.sampleRate();
    ma_uint64 total = src.totalFrames();

    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f) {
        if (!sink) fwprintf(stderr, L"Cannot create output: %ls\n", out.c_str());
        return 4;
    }

    vorbis_info vi;
    vorbis_info_init(&vi);
    int rc = 4;
    OggFileWriter w;
    vorbis_dsp_state vd;
    vorbis_block vb;
    bool haveVd = false, haveVb = false, haveOs = false;
    if (vorbis_encode_init_vbr(&vi, ch, rate, quality) != 0) {
        if (!sink) fwprintf(stderr, L"Vorbis setup failed.\n");
    } else {
        vorbis_comment vc;
        vorbis_comment_init(&vc);
        for (const auto& kv : fields)
            vorbis_comment_add_tag(&vc, ToVorbisName(kv.first).c_str(),
                                   U8(kv.second.c_str()).c_str());
        vorbis_analysis_init(&vd, &vi);
        haveVd = true;
        vorbis_block_init(&vd, &vb);
        haveVb = true;
        if (!w.begin(f, ((rand() & 0x7FFF) << 15) | (rand() & 0x7FFF) | 1)) {
            if (!sink) fwprintf(stderr, L"Ogg setup failed.\n");
        } else {
            haveOs = true;
            ogg_packet hp, hc, ht;
            vorbis_analysis_headerout(&vd, &vc, &hp, &hc, &ht);
            rc = 0;
            if (!w.packet(&hp, true) || !w.packet(&hc, true) || !w.packet(&ht, true)) rc = 4;
            vorbis_comment_clear(&vc);
            if (rc == 0) {
                FrameProgress prog(sink, total);
                std::vector<int16_t> pcm((size_t)2048 * (size_t)ch);
                ma_uint64 fed = 0;
                for (;;) {
                    if (prog.cancelled()) { rc = 5; break; }
                    int64_t got = src.readS16(pcm.data(), 2048);
                    if (got < 0) { rc = 4; break; }
                    float** buf = vorbis_analysis_buffer(&vd, 2048);
                    for (int64_t i = 0; i < got; i++)
                        for (int c = 0; c < ch; c++)
                            buf[c][i] = pcm[(size_t)i * (size_t)ch + (size_t)c] / 32768.0f;
                    vorbis_analysis_wrote(&vd, (int)got);
                    ogg_packet op;
                    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
                        vorbis_analysis(&vb, nullptr);
                        vorbis_bitrate_addblock(&vb);
                        while (vorbis_bitrate_flushpacket(&vd, &op)) {
                            if (!w.packet(&op)) { rc = 4; break; }
                        }
                        if (rc != 0) break;
                    }
                    if (rc != 0) break;
                    if (got > 0) {
                        fed += (ma_uint64)got;
                        prog.update(fed);
                    } else {
                        break; // EOF flag submitted; drain below
                    }
                }
                if (rc == 0) {
                    ogg_packet op;
                    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
                        vorbis_analysis(&vb, nullptr);
                        vorbis_bitrate_addblock(&vb);
                        while (vorbis_bitrate_flushpacket(&vd, &op)) {
                            if (!w.packet(&op)) { rc = 4; break; }
                        }
                        if (rc != 0) break;
                    }
                }
                prog.finish();
            }
        }
    }
    if (haveOs) w.end();
    if (haveVb) vorbis_block_clear(&vb);
    if (haveVd) vorbis_dsp_clear(&vd);
    vorbis_info_clear(&vi);
    fclose(f);
    if (rc != 0) DeleteFileW(out.c_str());
    return rc;
}

static void PutLe16(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void PutLe32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

// ---------------------------------------------------------------- Opus out (Ogg)
// Requires 48kHz stereo/mono PCM (dispatcher wraps resample/downmix).
int EncodeOpusFromPcm(PcmSource& src, const std::wstring& out, int bitrate, int origRate,
                      const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                      IEncodeProgress* sink) {
    if (bitrate < 6000) bitrate = 6000;
    if (bitrate > 510000) bitrate = 510000;
    int ch = src.channels(), rate = src.sampleRate();
    ma_uint64 total = src.totalFrames();
    if (rate != 48000 || ch < 1 || ch > 2) {
        if (!sink) fwprintf(stderr, L"Opus needs 48kHz mono/stereo PCM.\n");
        return 4;
    }

    int opusErr = 0;
    OpusEncoder* enc = opus_encoder_create(48000, ch, OPUS_APPLICATION_AUDIO, &opusErr);
    if (!enc || opusErr != OPUS_OK) {
        if (!sink) fwprintf(stderr, L"Opus setup failed.\n");
        if (enc) opus_encoder_destroy(enc);
        return 4;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(enc, OPUS_SET_VBR(1));
    opus_int32 lookahead = 0;
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));
    if (lookahead < 0) lookahead = 0;

    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f) {
        if (!sink) fwprintf(stderr, L"Cannot create output: %ls\n", out.c_str());
        opus_encoder_destroy(enc);
        return 4;
    }

    OggFileWriter w;
    int rc = 4;
    if (!w.begin(f, ((rand() & 0x7FFF) << 15) | (rand() & 0x7FFF) | 1)) {
        if (!sink) fwprintf(stderr, L"Ogg setup failed.\n");
    } else {
        // OpusHead (19 bytes, mapping family 0).
        unsigned char head[19];
        memcpy(head, "OpusHead", 8);
        head[8] = 1;
        head[9] = (unsigned char)ch;
        PutLe16(head + 10, (uint32_t)lookahead);
        PutLe32(head + 12, (uint32_t)origRate);
        PutLe16(head + 16, 0);
        head[18] = 0;
        ogg_packet op;
        memset(&op, 0, sizeof(op));
        op.packet = head;
        op.bytes = 19;
        op.b_o_s = 1;
        op.packetno = 0;
        // OpusTags. NOTE: the 8-byte magic is raw (no length prefix);
        // only the vendor string and comments are length-prefixed.
        std::vector<unsigned char> tags;
        auto putStr = [&](const char* s, size_t n) {
            unsigned char len[4];
            PutLe32(len, (uint32_t)n);
            tags.insert(tags.end(), len, len + 4);
            tags.insert(tags.end(), (const unsigned char*)s, (const unsigned char*)s + n);
        };
        tags.insert(tags.end(), {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'});
        putStr("AudioPlayer", 11);
        std::vector<std::string> comments;
        for (const auto& kv : fields)
            comments.push_back(ToVorbisName(kv.first) + "=" + U8(kv.second.c_str()));
        unsigned char cnt[4];
        PutLe32(cnt, (uint32_t)comments.size());
        tags.insert(tags.end(), cnt, cnt + 4);
        for (const auto& c : comments) putStr(c.c_str(), c.size());
        ogg_packet opTags;
        memset(&opTags, 0, sizeof(opTags));
        opTags.packet = tags.data();
        opTags.bytes = (long)tags.size();
        opTags.packetno = 1;

        rc = 0;
        if (!w.packet(&op, true) || !w.packet(&opTags, true)) rc = 4;
        if (rc == 0) {
            FrameProgress prog(sink, total);
            const int kFrame = 960; // 20ms @ 48kHz
            std::vector<int16_t> pcm((size_t)kFrame * (size_t)ch);
            std::vector<unsigned char> enc3(4000);
            ma_uint64 fed = 0, pktNo = 2;
            for (;;) {
                if (prog.cancelled()) { rc = 5; break; }
                int64_t got = src.readS16(pcm.data(), kFrame);
                if (got < 0) { rc = 4; break; }
                bool last = (got < kFrame);
                for (int64_t i = got; i < kFrame; i++)
                    for (int c = 0; c < ch; c++)
                        pcm[(size_t)i * (size_t)ch + (size_t)c] = 0;
                opus_int32 nb = opus_encode(enc, pcm.data(), kFrame, enc3.data(), 4000);
                if (nb < 0) { rc = 4; break; }
                fed += (ma_uint64)kFrame;
                ogg_packet ap;
                memset(&ap, 0, sizeof(ap));
                ap.packet = enc3.data();
                ap.bytes = nb;
                ap.packetno = (long)pktNo++;
                ap.granulepos = (ogg_int64_t)fed;
                if (last) {
                    ap.e_o_s = 1;
                    ap.granulepos = fed >= (ma_uint64)lookahead ? (ogg_int64_t)(fed - lookahead)
                                                                : 0;
                }
                if (!w.packet(&ap, last)) { rc = 4; break; }
                prog.update(fed);
                if (last) break;
            }
            prog.finish();
        }
    }
    w.end();
    opus_encoder_destroy(enc);
    fclose(f);
    if (rc != 0) DeleteFileW(out.c_str());
    return rc;
}

// ---------------------------------------------------------------- MP3 out (LAME CBR)
// Requires <=2ch and a standard MPEG rate (dispatcher resamples otherwise).
int EncodeMp3FromPcm(PcmSource& src, const std::wstring& out, int bitrateKbps,
                     const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                     IEncodeProgress* sink) {
    int br = NearestMp3Bitrate(bitrateKbps);
    int ch = src.channels(), rate = src.sampleRate();
    ma_uint64 total = src.totalFrames();
    if (ch < 1 || ch > 2 || !LameRateOk(rate)) {
        if (!sink) fwprintf(stderr, L"MP3 needs mono/stereo at a standard MPEG rate.\n");
        return 4;
    }

    lame_t gfp = lame_init();
    if (!gfp) return 4;
    lame_set_in_samplerate(gfp, rate);
    lame_set_num_channels(gfp, ch);
    lame_set_brate(gfp, br);
    lame_set_quality(gfp, 2); // high quality, reasonable speed
    id3tag_init(gfp);
    for (const auto& kv : fields) {
        std::string v = U8(kv.second.c_str());
        if (SameInternal(kv.first, L"Title")) id3tag_set_title(gfp, v.c_str());
        else if (SameInternal(kv.first, L"Artist")) id3tag_set_artist(gfp, v.c_str());
        else if (SameInternal(kv.first, L"Album")) id3tag_set_album(gfp, v.c_str());
        else if (SameInternal(kv.first, L"Year")) id3tag_set_year(gfp, v.c_str());
        else if (SameInternal(kv.first, L"Comment")) id3tag_set_comment(gfp, v.c_str());
        else if (SameInternal(kv.first, L"Track")) id3tag_set_track(gfp, v.c_str());
        else if (SameInternal(kv.first, L"Genre")) id3tag_set_genre(gfp, v.c_str());
    }
    id3tag_add_v2(gfp);
    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        return 4;
    }

    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f) {
        if (!sink) fwprintf(stderr, L"Cannot create output: %ls\n", out.c_str());
        lame_close(gfp);
        return 4;
    }
    int rc = 0;
    {
        // ID3v2 tag first (buffer sized generously; LAME reports actual).
        std::vector<unsigned char> tag(128 * 1024);
        size_t tagLen = lame_get_id3v2_tag(gfp, tag.data(), tag.size());
        if (tagLen > tag.size()) tagLen = 0; // shouldn't happen; skip tag
        if (tagLen > 0 && fwrite(tag.data(), 1, tagLen, f) != tagLen) rc = 4;
    }
    if (rc == 0) {
        FrameProgress prog(sink, total);
        const int kChunk = 8192;
        std::vector<int16_t> pcm((size_t)kChunk * (size_t)ch);
        std::vector<unsigned char> mp3(16384);
        ma_uint64 fed = 0;
        for (;;) {
            if (prog.cancelled()) { rc = 5; break; }
            int64_t got = src.readS16(pcm.data(), kChunk);
            if (got < 0) { rc = 4; break; }
            if (got == 0) break;
            int nb = lame_encode_buffer_interleaved(gfp, pcm.data(), (int)got, mp3.data(),
                                                    (int)mp3.size());
            if (nb < 0) { rc = 4; break; }
            if (nb > 0 && fwrite(mp3.data(), 1, (size_t)nb, f) != (size_t)nb) { rc = 4; break; }
            fed += (ma_uint64)got;
            prog.update(fed);
        }
        if (rc == 0) {
            int nb = lame_encode_flush(gfp, mp3.data(), (int)mp3.size());
            if (nb < 0) rc = 4;
            else if (nb > 0 && fwrite(mp3.data(), 1, (size_t)nb, f) != (size_t)nb) rc = 4;
        }
        prog.finish();
    }
    lame_close(gfp);
    fclose(f);
    if (rc != 0) DeleteFileW(out.c_str());
    return rc;
}

// ---------------------------------------------------------------- Opus input
// Minimal Ogg-Opus demuxer (mapping family 0: mono/stereo) + libopus
// decoder. Always delivers 48kHz s16, the format Opus streams carry.
class OpusPcmSource : public PcmSource {
public:
    ~OpusPcmSource() override { close(); }
    bool open(const std::wstring& path, std::wstring& err) override {
        m_f = _wfopen(path.c_str(), L"rb");
        if (!m_f) {
            err = L"Cannot open file.";
            return false;
        }
        if (!scan(err)) {
            close();
            return false;
        }
        // Second pass: rewind and decode.
        fseek(m_f, 0, SEEK_SET);
        ogg_sync_reset(&m_oy);
        ogg_stream_reset(&m_os);
        m_dec = opus_decoder_create(48000, m_ch, &m_opusErr);
        if (!m_dec || m_opusErr != OPUS_OK) {
            err = L"Opus setup failed.";
            close();
            return false;
        }
        m_packetNo = 0;
        m_dropped = 0;
        m_served = 0;
        m_eos = false;
        return true;
    }
    int channels() const override { return m_ch; }
    int sampleRate() const override { return 48000; }
    ma_uint64 totalFrames() const override { return m_total; }
    bool seekBlock(int64_t block) override {
        if (!m_f) return false;
        if (block < 0) block = 0;
        if ((ma_uint64)block >= m_total) block = (int64_t)m_total - 1;
        if (block < 0) block = 0;
        // Restart the decode from the top and skip forward. Opus has no
        // cheap random access in this demuxer; full-file skips on typical
        // songs still complete in well under a second.
        fseek(m_f, 0, SEEK_SET);
        ogg_sync_reset(&m_oy);
        ogg_stream_reset(&m_os);
        if (m_dec) opus_decoder_destroy(m_dec);
        m_dec = opus_decoder_create(48000, m_ch, &m_opusErr);
        if (!m_dec || m_opusErr != OPUS_OK) {
            m_dec = nullptr;
            return false;
        }
        m_fifo.clear();
        m_fifoOff = 0;
        m_dropped = 0;
        m_served = 0;
        m_eos = false;
        // Skip packets until the header pair is past (they're filtered in
        // fillFifo anyway), then discard `block` output samples.
        int64_t left = block;
        std::vector<int16_t> tmp((size_t)4096 * (size_t)m_ch);
        while (left > 0) {
            uint64_t want = (uint64_t)left > 4096 ? 4096 : (uint64_t)left;
            int64_t got = readS16(tmp.data(), want);
            if (got <= 0) break;
            left -= got;
        }
        m_served = (ma_uint64)(block - left);
        return left == 0;
    }
    int64_t readS16(int16_t* dst, uint64_t wantFrames) override {
        uint64_t done = 0;
        while (done < wantFrames) {
            if (m_served >= m_total) break;
            if (m_fifoOff >= m_fifo.size() && !fillFifo()) break;
            uint64_t avail = (m_fifo.size() - m_fifoOff) / (uint64_t)m_ch;
            uint64_t left = m_total - m_served;
            uint64_t take = wantFrames - done;
            if (take > avail) take = avail;
            if (take > left) take = left;
            if (take == 0) break;
            memcpy(dst + done * (uint64_t)m_ch, m_fifo.data() + m_fifoOff,
                   (size_t)take * (size_t)m_ch * 2);
            m_fifoOff += take * (uint64_t)m_ch;
            done += take;
            m_served += take;
            if (m_fifoOff >= m_fifo.size()) {
                m_fifo.clear();
                m_fifoOff = 0;
            }
        }
        return (int64_t)done;
    }
    void extraTags(const std::wstring& path,
                   std::vector<std::pair<std::wstring, std::wstring>>& fields) const override {
        (void)path;
        for (const auto& kv : m_tags) fields.push_back(kv);
    }

private:
    void close() {
        if (m_dec) {
            opus_decoder_destroy(m_dec);
            m_dec = nullptr;
        }
        if (m_osInit) {
            ogg_stream_clear(&m_os);
            m_osInit = false;
        }
        ogg_sync_clear(&m_oy);
        memset(&m_oy, 0, sizeof(m_oy));
        if (m_f) {
            fclose(m_f);
            m_f = nullptr;
        }
    }
    bool readMore() {
        char* buf = ogg_sync_buffer(&m_oy, 4096);
        if (!buf) return false;
        size_t n = fread(buf, 1, 4096, m_f);
        if (n == 0) return false;
        ogg_sync_wrote(&m_oy, (long)n);
        return true;
    }
    // First pass: locate the Opus stream, read OpusHead/OpusTags, and find
    // the max granule position for the total length.
    bool scan(std::wstring& err) {
        ogg_sync_init(&m_oy);
        ogg_page og;
        bool foundHead = false;
        ogg_int64_t maxGran = 0;
        for (;;) {
            int r = ogg_sync_pageout(&m_oy, &og);
            if (r == 0) {
                if (!readMore()) break;
                continue;
            }
            if (r < 0) continue; // hole: skip
            int serial = ogg_page_serialno(&og);
            if (!m_osInit) {
                ogg_stream_init(&m_os, serial);
                m_osInit = true;
                m_serial = serial;
            }
            if (serial != m_serial) continue;
            ogg_stream_pagein(&m_os, &og);
            ogg_packet op;
            while (ogg_stream_packetout(&m_os, &op) == 1) {
                if (!foundHead) {
                    if (op.bytes < 19 || memcmp(op.packet, "OpusHead", 8) != 0 || op.packet[8] != 1) {
                        err = L"Not an Ogg Opus file.";
                        return false;
                    }
                    m_ch = op.packet[9];
                    m_preskip = (unsigned)(op.packet[10] | (op.packet[11] << 8));
                    m_mapping = op.packet[18];
                    if ((m_ch != 1 && m_ch != 2) || m_mapping != 0) {
                        err = L"Only mono/stereo Opus is supported.";
                        return false;
                    }
                    foundHead = true;
                } else if (!m_haveTags) {
                    if (op.bytes >= 8 && memcmp(op.packet, "OpusTags", 8) == 0)
                        parseTags(op.packet, (size_t)op.bytes);
                    m_haveTags = true;
                }
            }
            if (foundHead && ogg_page_granulepos(&og) > maxGran)
                maxGran = ogg_page_granulepos(&og);
            if (ogg_page_eos(&og)) break;
        }
        if (!foundHead) {
            err = L"Not an Ogg Opus file.";
            return false;
        }
        m_total = maxGran > (ogg_int64_t)m_preskip ? (ma_uint64)(maxGran - m_preskip) : 0;
        if (m_total == 0) {
            err = L"Empty Opus stream.";
            return false;
        }
        return true;
    }
    void parseTags(const unsigned char* p, size_t n) {
        if (n < 16) return;
        size_t pos = 8;
        uint32_t vlen = Le32(p + pos);
        pos += 4;
        if (pos + vlen > n) return;
        pos += vlen;
        if (pos + 4 > n) return;
        uint32_t count = Le32(p + pos);
        pos += 4;
        for (uint32_t i = 0; i < count && pos + 4 <= n; i++) {
            uint32_t len = Le32(p + pos);
            pos += 4;
            if (pos + len > n) break;
            SplitComment((const char*)p + pos, len, m_tags);
            pos += len;
            if (m_tags.size() > 64) break;
        }
    }
    // Decode audio packets until the FIFO holds output or the stream ends.
    // Packets are drained oldest-first: the drain runs before pulling new
    // pages, and keeps running at EOF, so densely packed pages (many
    // packets per page, as our encoder writes) decode in full.
    bool fillFifo() {
        static const int kMaxFrame = 5760; // 120ms @ 48kHz
        if (m_frame.size() < (size_t)kMaxFrame * (size_t)m_ch)
            m_frame.resize((size_t)kMaxFrame * (size_t)m_ch);
        int16_t* frame = m_frame.data();
        ogg_page og;
        ogg_packet op;
        for (;;) {
            int porc = 0;
            while ((porc = ogg_stream_packetout(&m_os, &op)) != 0) {
                if (porc < 0) {
                    continue; // gap: codec handles the missing packet itself
                }
                if (op.bytes >= 8 &&
                    (memcmp(op.packet, "OpusHead", 8) == 0 || memcmp(op.packet, "OpusTags", 8) == 0))
                    continue;
                int n = opus_decode(m_dec, op.packet, op.bytes, frame, kMaxFrame, 0);
                if (n < 0) {
                    continue; // damaged packet: skip
                }
                size_t base = m_fifo.size();
                m_fifo.resize(base + (size_t)n * (size_t)m_ch);
                memcpy(m_fifo.data() + base, frame, (size_t)n * (size_t)m_ch * 2);
                // Drop the codec pre-skip from the very start (new samples only).
                if (m_dropped < m_preskip) {
                    size_t drop = (size_t)(m_preskip - m_dropped);
                    if (drop > (size_t)n) drop = (size_t)n;
                    m_fifo.erase(m_fifo.begin() + base,
                                 m_fifo.begin() + base + drop * (size_t)m_ch);
                    m_dropped += (unsigned)drop;
                }
                if (op.e_o_s) m_eos = true;
                if (!m_fifo.empty()) return true;
                // Fully pre-skip-dropped packet: keep draining.
            }
            int r = ogg_sync_pageout(&m_oy, &og);
            if (r == 0) {
                if (!readMore()) {
                    m_eos = true;
                    return !m_fifo.empty();
                }
                continue;
            }
            if (r < 0) continue;
            if (ogg_page_serialno(&og) != m_serial) continue;
            ogg_stream_pagein(&m_os, &og);
            if (ogg_page_eos(&og)) m_eos = true;
        }
    }

    FILE* m_f = nullptr;
    ogg_sync_state m_oy;
    ogg_stream_state m_os;
    bool m_osInit = false;
    int m_serial = 0;
    OpusDecoder* m_dec = nullptr;
    int m_opusErr = 0;
    int m_ch = 0;
    unsigned m_preskip = 0;
    unsigned m_mapping = 0;
    bool m_haveTags = false;
    std::vector<std::pair<std::wstring, std::wstring>> m_tags;
    ma_uint64 m_total = 0;
    std::vector<int16_t> m_fifo;
    size_t m_fifoOff = 0;
    std::vector<int16_t> m_frame; // reused decode scratch (no per-call alloc)
    unsigned m_packetNo = 0;
    unsigned m_dropped = 0;
    ma_uint64 m_served = 0;
    bool m_eos = false;
};

std::unique_ptr<PcmSource> OpenOpusSource(const std::wstring& path, std::wstring& err) {
    std::unique_ptr<OpusPcmSource> src(new OpusPcmSource());
    if (!src->open(path, err)) return nullptr;
    return std::unique_ptr<PcmSource>(src.release());
}

std::unique_ptr<PcmSource> CreateOpusSource() {
    return std::unique_ptr<PcmSource>(new OpusPcmSource());
}

// ---------------------------------------------------------------- Vorbis input
// Ogg Vorbis PCM source via libvorbisfile (miniaudio 0.11.25 ships no
// Vorbis decoder backend, so .ogg goes here instead of MaPcmSource).
class OggVorbisPcmSource : public PcmSource {
public:
    OggVorbisPcmSource() { memset(&m_vf, 0, sizeof(m_vf)); }
    ~OggVorbisPcmSource() override {
        if (m_open) ov_clear(&m_vf); // also closes m_f
    }
    bool open(const std::wstring& path, std::wstring& err) override {
        m_f = _wfopen(path.c_str(), L"rb");
        if (!m_f) {
            err = L"Cannot open file.";
            return false;
        }
        if (ov_open_callbacks(m_f, &m_vf, nullptr, 0, OV_CALLBACKS_DEFAULT) < 0) {
            fclose(m_f);
            m_f = nullptr;
            err = L"Not a valid Ogg Vorbis file.";
            return false;
        }
        m_open = true; // ov_clear now owns m_f
        m_f = nullptr;
        vorbis_info* vi = ov_info(&m_vf, -1);
        if (!vi || vi->channels < 1 || vi->channels > 8 || vi->rate <= 0) {
            err = L"Unsupported Vorbis stream.";
            return false;
        }
        m_ch = vi->channels;
        m_rate = (int)vi->rate;
        ogg_int64_t total = ov_pcm_total(&m_vf, -1);
        m_total = total > 0 ? (ma_uint64)total : kUnknownTotal;
        vorbis_comment* vc = ov_comment(&m_vf, -1);
        if (vc) {
            for (int i = 0; i < vc->comments && (int)m_tags.size() < 64; i++) {
                const char* c = vc->user_comments[i];
                int n = vc->comment_lengths[i];
                if (c && n > 0) SplitComment(c, (size_t)n, m_tags);
            }
        }
        return true;
    }
    int channels() const override { return m_ch; }
    int sampleRate() const override { return m_rate; }
    ma_uint64 totalFrames() const override { return m_total; }
    bool seekBlock(int64_t block) override {
        if (!m_open) return false;
        if (block < 0) block = 0;
        return ov_pcm_seek(&m_vf, block) == 0;
    }
    int64_t readS16(int16_t* dst, uint64_t wantFrames) override {
        uint64_t done = 0;
        int bitstream = 0;
        while (done < wantFrames) {
            uint64_t chunk = wantFrames - done;
            if (chunk > 4096) chunk = 4096;
            long got = ov_read(&m_vf, (char*)(dst + done * (uint64_t)m_ch),
                               (int)(chunk * (uint64_t)m_ch * 2), 0, 2, 1, &bitstream);
            if (got < 0) continue; // hole: skip damaged packet
            if (got == 0) break;   // EOF
            done += (uint64_t)got / ((uint64_t)m_ch * 2);
        }
        return (int64_t)done;
    }
    void extraTags(const std::wstring& path,
                   std::vector<std::pair<std::wstring, std::wstring>>& fields) const override {
        (void)path;
        for (const auto& kv : m_tags) fields.push_back(kv);
    }

private:
    FILE* m_f = nullptr;
    OggVorbis_File m_vf;
    bool m_open = false;
    int m_ch = 0, m_rate = 0;
    ma_uint64 m_total = kUnknownTotal;
    std::vector<std::pair<std::wstring, std::wstring>> m_tags;
};

std::unique_ptr<PcmSource> OpenOggSource(const std::wstring& path, std::wstring& err) {
    std::unique_ptr<OggVorbisPcmSource> src(new OggVorbisPcmSource());
    if (!src->open(path, err)) return nullptr;
    return std::unique_ptr<PcmSource>(src.release());
}

std::unique_ptr<PcmSource> CreateOggSource() {
    return std::unique_ptr<PcmSource>(new OggVorbisPcmSource());
}

// ---------------------------------------------------------------- tag readers
bool ReadVorbisFileTags(const std::wstring& path,
                        std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    OggVorbis_File vf;
    memset(&vf, 0, sizeof(vf));
    if (ov_open_callbacks(f, &vf, nullptr, 0, OV_CALLBACKS_DEFAULT) < 0) {
        fclose(f);
        return false;
    }
    vorbis_comment* vc = ov_comment(&vf, -1);
    if (vc) {
        for (int i = 0; i < vc->comments && (int)fields.size() < 64; i++) {
            const char* c = vc->user_comments[i];
            int n = vc->comment_lengths[i];
            if (c && n > 0) SplitComment(c, (size_t)n, fields);
        }
    }
    ov_clear(&vf); // also closes f
    return true;
}

bool ReadFlacTags(const std::wstring& path,
                  std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    HANDLE h =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    auto readAll = [&](void* buf, DWORD n) {
        DWORD got = 0;
        return ReadFile(h, buf, n, &got, nullptr) && got == n;
    };
    bool ok = false;
    unsigned char magic[4];
    if (readAll(magic, 4) && memcmp(magic, "fLaC", 4) == 0) {
        LARGE_INTEGER scanned = {};
        for (;;) {
            unsigned char hdr[4];
            if (!readAll(hdr, 4)) break;
            bool last = (hdr[0] & 0x80) != 0;
            unsigned type = hdr[0] & 0x7F;
            uint32_t len = ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
            if (type == 4 && len >= 8 && len <= 1024 * 1024) {
                std::vector<unsigned char> blk(len);
                if (!readAll(blk.data(), len)) break;
                size_t pos = 0;
                uint32_t vlen = Le32(blk.data() + pos);
                pos += 4;
                if (pos + vlen <= len) {
                    pos += vlen;
                    if (pos + 4 <= len) {
                        uint32_t count = Le32(blk.data() + pos);
                        pos += 4;
                        for (uint32_t i = 0; i < count && pos + 4 <= len; i++) {
                            uint32_t elen = Le32(blk.data() + pos);
                            pos += 4;
                            if (pos + elen > len || elen > 256 * 1024) break;
                            SplitComment((const char*)blk.data() + pos, elen, fields);
                            pos += elen;
                            if (fields.size() > 64) break;
                        }
                        ok = true;
                    }
                }
                break; // comment block handled (or failed): stop
            }
            LARGE_INTEGER skip;
            skip.QuadPart = len;
            if (!SetFilePointerEx(h, skip, nullptr, FILE_CURRENT)) break;
            scanned.QuadPart += len;
            if (scanned.QuadPart > 4 * 1024 * 1024) break; // sanity cap
            if (last) break;
        }
        if (!ok) ok = true; // valid FLAC, just no parsable comment block
    }
    CloseHandle(h);
    return ok;
}

bool ReadWavTags(const std::wstring& path,
                 std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    auto readAll = [&](void* buf, size_t n) { return fread(buf, 1, n, f) == n; };
    bool examined = false;
    unsigned char riff[12];
    if (readAll(riff, 12) && memcmp(riff, "RIFF", 4) == 0 && memcmp(riff + 8, "WAVE", 4) == 0) {
        examined = true;
        for (int chunks = 0; chunks < 256; chunks++) {
            unsigned char hdr[8];
            if (!readAll(hdr, 8)) break;
            uint32_t size = Le32(hdr + 4);
            long dataPos = ftell(f);
            if (memcmp(hdr, "LIST", 4) == 0 && size >= 4) {
                unsigned char type[4];
                if (!readAll(type, 4)) break;
                if (memcmp(type, "INFO", 4) == 0) {
                    uint32_t left = size - 4;
                    while (left >= 8) {
                        unsigned char sh[8];
                        if (!readAll(sh, 8)) break;
                        uint32_t slen = Le32(sh + 4);
                        if (slen > left || slen > 64 * 1024) break;
                        std::vector<char> val(slen + 1, 0);
                        if (slen > 0 && !readAll(val.data(), slen)) break;
                        if (slen & 1) {
                            char pad = 0;
                            if (!readAll(&pad, 1)) break;
                        }
                        const wchar_t* name = nullptr;
                        if (memcmp(sh, "INAM", 4) == 0) name = L"Title";
                        else if (memcmp(sh, "IART", 4) == 0) name = L"Artist";
                        else if (memcmp(sh, "IPRD", 4) == 0) name = L"Album";
                        else if (memcmp(sh, "IGNR", 4) == 0) name = L"Genre";
                        else if (memcmp(sh, "ICRD", 4) == 0) name = L"Year";
                        else if (memcmp(sh, "IPRT", 4) == 0) name = L"Track";
                        else if (memcmp(sh, "ICMT", 4) == 0) name = L"Comment";
                        if (name) {
                            std::wstring v = WidenTagBytes(val.data(), slen);
                            if (!v.empty()) fields.push_back({name, v});
                        }
                        uint32_t consumed = 8 + slen + (slen & 1);
                        left -= consumed;
                    }
                }
                break; // only first LIST examined
            }
            if (fseek(f, dataPos + (long)((size + 1) & ~1u), SEEK_SET) != 0) break;
        }
    }
    fclose(f);
    return examined;
}

// Appends a LIST INFO chunk after the audio data and fixes the RIFF size.
// Returns 0 always - tags must never fail an otherwise good encode.
int WriteWavTags(const std::wstring& wavFile,
                 const std::vector<std::pair<std::wstring, std::wstring>>& fields) {
    if (fields.empty()) return 0;
    static const struct {
        const wchar_t* internal;
        const char id[4];
    } kMap[] = {{L"Title", {'I', 'N', 'A', 'M'}}, {L"Artist", {'I', 'A', 'R', 'T'}},
                {L"Album", {'I', 'P', 'R', 'D'}}, {L"Genre", {'I', 'G', 'N', 'R'}},
                {L"Year", {'I', 'C', 'R', 'D'}},  {L"Track", {'I', 'P', 'R', 'T'}},
                {L"Comment", {'I', 'C', 'M', 'T'}}};
    std::vector<unsigned char> payload;
    payload.insert(payload.end(), {'I', 'N', 'F', 'O'});
    for (const auto& m : kMap) {
        for (const auto& kv : fields) {
            if (_wcsicmp(kv.first.c_str(), m.internal) != 0) continue;
            std::string v = U8(kv.second.c_str());
            if (v.empty() || v.size() > 4096) break;
            payload.insert(payload.end(), m.id, m.id + 4);
            unsigned char len[4];
            PutLe32(len, (uint32_t)v.size() + 1); // NUL-terminated
            payload.insert(payload.end(), len, len + 4);
            payload.insert(payload.end(), v.begin(), v.end());
            payload.push_back(0);
            if (payload.size() & 1) payload.push_back(0);
            break;
        }
    }
    if (payload.size() <= 4) return 0; // nothing mapped
    FILE* f = _wfopen(wavFile.c_str(), L"r+b");
    if (!f) return 0;
    unsigned char riff[12];
    bool ok = false;
    if (fread(riff, 1, 12, f) == 12 && memcmp(riff, "RIFF", 4) == 0 &&
        memcmp(riff + 8, "WAVE", 4) == 0) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long endPos = ftell(f);
            if (endPos > 12) {
                unsigned char hdr[8] = {'L', 'I', 'S', 'T', 0, 0, 0, 0};
                PutLe32(hdr + 4, (uint32_t)payload.size());
                if (fwrite(hdr, 1, 8, f) == 8 && fwrite(payload.data(), 1, payload.size(), f) == payload.size()) {
                    long newEnd = ftell(f);
                    if (newEnd > 8) {
                        unsigned char riffSize[4];
                        PutLe32(riffSize, (uint32_t)(newEnd - 8));
                        if (fseek(f, 4, SEEK_SET) == 0 && fwrite(riffSize, 1, 4, f) == 4)
                            ok = true;
                    }
                }
            }
        }
    }
    (void)ok;
    fclose(f);
    return 0;
}
