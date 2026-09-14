// Extra format engines: FLAC / Vorbis / Opus / MP3 outputs, Opus input,
// and the tag readers/writers for non-APE formats. See encode_extra.h.
#pragma once

#include "encode.h"
#include "encode_pcm.h"

#include <string>
#include <vector>
#include <utility>
#include <memory>

// Same calling convention as the APE/WAV engines in encode.cpp:
// 0 ok, 4 encode failure, 5 cancelled. `fields` are carried as
// (InternalName, value) pairs; each engine maps them to its native tags.
int EncodeFlacFromPcm(PcmSource& src, const std::wstring& out, int flacLevel,
                      const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                      IEncodeProgress* sink);
int EncodeVorbisFromPcm(PcmSource& src, const std::wstring& out, float quality,
                        const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                        IEncodeProgress* sink);
// Requires 48kHz, <=2ch (dispatcher wraps ResampleSource/DownmixSource).
// origRate is stored in the OpusHead header for the record.
int EncodeOpusFromPcm(PcmSource& src, const std::wstring& out, int bitrate, int origRate,
                      const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                      IEncodeProgress* sink);
// Requires <=2ch and a standard MPEG rate (dispatcher resamples otherwise).
int EncodeMp3FromPcm(PcmSource& src, const std::wstring& out, int bitrateKbps,
                     const std::vector<std::pair<std::wstring, std::wstring>>& fields,
                     IEncodeProgress* sink);

// Opus-in-Ogg PCM source (used by OpenPcmSource for .opus).
std::unique_ptr<PcmSource> OpenOpusSource(const std::wstring& path, std::wstring& err);
// Unopened instance for the probe -> wrap -> open pattern in EncodeToFile.
std::unique_ptr<PcmSource> CreateOpusSource();

// Vorbis-in-Ogg PCM source via libvorbisfile (used for .ogg/.oga).
std::unique_ptr<PcmSource> OpenOggSource(const std::wstring& path, std::wstring& err);
// Unopened instance for the probe -> wrap -> open pattern in EncodeToFile.
std::unique_ptr<PcmSource> CreateOggSource();

// Tag readers for the extra input formats. True = file examined (tags may
// still be empty); appends (InternalName, value) pairs.
bool ReadVorbisFileTags(const std::wstring& path,
                        std::vector<std::pair<std::wstring, std::wstring>>& fields); // .ogg
bool ReadFlacTags(const std::wstring& path,
                  std::vector<std::pair<std::wstring, std::wstring>>& fields); // .flac
bool ReadWavTags(const std::wstring& path,
                 std::vector<std::pair<std::wstring, std::wstring>>& fields); // .wav

// Appends a LIST INFO chunk to an existing WAV file. Returns 0 always -
// tags must never fail an otherwise good encode.
int WriteWavTags(const std::wstring& wavFile,
                 const std::vector<std::pair<std::wstring, std::wstring>>& fields);

// Nearest valid LAME CBR bitrate (kbps) for a requested value.
int NearestMp3Bitrate(int want);

// True for the MPEG-1/2/2.5 sample rates LAME accepts.
bool LameRateOk(int rate);
