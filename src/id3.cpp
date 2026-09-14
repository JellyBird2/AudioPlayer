// ID3v1 + ID3v2.2/2.3/2.4 text-frame reader. See id3.h.
#include "id3.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

std::wstring WidenLatin1(const char* p, size_t n) {
    // Latin-1 maps 1:1 onto U+0000..U+00FF.
    std::wstring w;
    w.reserve(n);
    for (size_t i = 0; i < n; i++) w.push_back((wchar_t)(unsigned char)p[i]);
    return w;
}

std::wstring DecodeUtf8(const char* p, size_t n) {
    if (n == 0) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, nullptr, 0);
    if (need <= 0) return WidenLatin1(p, n);
    std::wstring w((size_t)need, 0);
    MultiByteToWideChar(CP_UTF8, 0, p, (int)n, w.data(), need);
    return w;
}

std::wstring DecodeUtf16(const char* p, size_t n, bool bigEndian) {
    std::wstring w;
    if (n < 2) return w;
    size_t i = 0;
    // Skip BOM if present.
    if (n >= 2) {
        unsigned char b0 = (unsigned char)p[0], b1 = (unsigned char)p[1];
        if (b0 == 0xFF && b1 == 0xFE) { bigEndian = false; i = 2; }
        else if (b0 == 0xFE && b1 == 0xFF) { bigEndian = true; i = 2; }
    }
    size_t rest = n - i;
    // Real-world quirk (e.g. lame): BOM followed by single-byte text.
    // Genuine UTF-16 of ASCII-range text always has NULs in every other
    // byte, and is always even-length - otherwise it's Latin-1 in disguise.
    bool allAscii = true;
    size_t zeros = 0;
    for (size_t k = i; k < n; k++) {
        unsigned char b = (unsigned char)p[k];
        if (b >= 0x80) { allAscii = false; break; }
        if (b == 0) zeros++;
    }
    if ((rest & 1) || (allAscii && zeros * 4 < rest)) {
        std::wstring v = WidenLatin1(p + i, rest);
        while (!v.empty() && (v.back() == 0 || v.back() == L' ')) v.pop_back();
        return v;
    }
    for (; i + 1 < n; i += 2) {
        wchar_t ch;
        if (bigEndian) ch = (wchar_t)(((unsigned char)p[i] << 8) | (unsigned char)p[i + 1]);
        else ch = (wchar_t)(((unsigned char)p[i + 1] << 8) | (unsigned char)p[i]);
        if (ch == 0) break;
        w.push_back(ch);
    }
    return w;
}

std::wstring DecodeTextFrame(const char* p, size_t n) {
    if (n < 1) return std::wstring();
    unsigned char enc = (unsigned char)p[0];
    const char* body = p + 1;
    size_t bodyLen = n - 1;
    std::wstring w;
    switch (enc) {
        case 0: w = WidenLatin1(body, bodyLen); break;
        case 1: w = DecodeUtf16(body, bodyLen, false); break;
        case 2: w = DecodeUtf16(body, bodyLen, true); break;
        case 3: w = DecodeUtf8(body, bodyLen); break;
        default: w = WidenLatin1(body, bodyLen); break;
    }
    // Trim trailing NULs/spaces.
    while (!w.empty() && (w.back() == 0 || w.back() == L' ')) w.pop_back();
    return w;
}

std::wstring TrimField(const char* p, size_t n) {
    // ID3v1 fields are space- or NUL-padded.
    while (n > 0 && (p[n - 1] == 0 || p[n - 1] == ' ')) n--;
    return WidenLatin1(p, n);
}

bool ReadAt(FILE* f, long long off, void* buf, size_t bytes) {
    if (fseek(f, (long)off, SEEK_SET) != 0) return false;
    return fread(buf, 1, bytes, f) == bytes;
}

unsigned SyncSafe(const unsigned char b[4]) {
    return ((unsigned)b[0] << 21) | ((unsigned)b[1] << 14) |
           ((unsigned)b[2] << 7) | (unsigned)b[3];
}

unsigned BE32(const unsigned char b[4]) {
    return ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) |
           ((unsigned)b[2] << 8) | (unsigned)b[3];
}

// Standard ID3v1 genre list (0-147 + a few common Winamp extensions).
const wchar_t* GenreName(int idx) {
    static const wchar_t* names[] = {
        L"Blues", L"Classic Rock", L"Country", L"Dance", L"Disco", L"Funk",
        L"Grunge", L"Hip-Hop", L"Jazz", L"Metal", L"New Age", L"Oldies",
        L"Other", L"Pop", L"R&B", L"Rap", L"Reggae", L"Rock", L"Techno",
        L"Industrial", L"Alternative", L"Ska", L"Death Metal", L"Pranks",
        L"Soundtrack", L"Euro-Techno", L"Ambient", L"Trip-Hop", L"Vocal",
        L"Jazz+Funk", L"Fusion", L"Trance", L"Classical", L"Instrumental",
        L"Acid", L"House", L"Game", L"Sound Clip", L"Gospel", L"Noise",
        L"AlternRock", L"Bass", L"Soul", L"Punk", L"Space", L"Meditative",
        L"Instrumental Pop", L"Instrumental Rock", L"Ethnic", L"Gothic",
        L"Darkwave", L"Techno-Industrial", L"Electronic", L"Pop-Folk",
        L"Eurodance", L"Dream", L"Southern Rock", L"Comedy", L"Cult",
        L"Gangsta", L"Top 40", L"Christian Rap", L"Pop/Funk", L"Jungle",
        L"Native American", L"Cabaret", L"New Wave", L"Psychedelic",
        L"Rave", L"Showtunes", L"Trailer", L"Lo-Fi", L"Tribal",
        L"Acid Punk", L"Acid Jazz", L"Polka", L"Retro", L"Musical",
        L"Rock & Roll", L"Hard Rock", L"Folk", L"Folk-Rock",
        L"National Folk", L"Swing", L"Fast Fusion", L"Bebob", L"Latin",
        L"Revival", L"Celtic", L"Bluegrass", L"Avantgarde",
        L"Gothic Rock", L"Progressive Rock", L"Psychedelic Rock",
        L"Symphonic Rock", L"Slow Rock", L"Big Band", L"Chorus",
        L"Easy Listening", L"Acoustic", L"Humour", L"Speech", L"Chanson",
        L"Opera", L"Chamber Music", L"Sonata", L"Symphony", L"Booty Bass",
        L"Primus", L"Porn Groove", L"Satire", L"Slow Jam", L"Club",
        L"Tango", L"Samba", L"Folklore", L"Ballad", L"Power Ballad",
        L"Rhythmic Soul", L"Freestyle", L"Duet", L"Punk Rock", L"Drum Solo",
        L"A Cappella", L"Euro-House", L"Dance Hall", L"Goa", L"Drum & Bass",
        L"Club-House", L"Hardcore", L"Terror", L"Indie", L"BritPop",
        L"Negerpunk", L"Polsk Punk", L"Beat", L"Christian Gangsta Rap",
        L"Heavy Metal", L"Black Metal", L"Crossover", L"Contemporary Christian",
        L"Christian Rock", L"Merengue", L"Salsa", L"Thrash Metal",
        L"Anime", L"JPop", L"Synthpop",
    };
    const int count = (int)(sizeof(names) / sizeof(names[0]));
    if (idx >= 0 && idx < count) return names[idx];
    return nullptr;
}

void ReadV1(FILE* f, long long fileSize, ID3Tags& out) {
    if (fileSize < 128) return;
    unsigned char tag[128];
    if (!ReadAt(f, fileSize - 128, tag, 128)) return;
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G') return;
    auto take = [&](std::wstring& dst, int off, int len) {
        std::wstring v = TrimField((const char*)tag + off, (size_t)len);
        if (!v.empty() && dst.empty()) dst = v;
    };
    take(out.title, 3, 30);
    take(out.artist, 33, 30);
    take(out.album, 63, 30);
    take(out.year, 93, 4);
    if (tag[125] == 0 && tag[126] != 0) {
        // v1.1: comment is 28 bytes + zero + track byte.
        take(out.comment, 97, 28);
        if (out.track.empty()) {
            wchar_t tb[16];
            swprintf_s(tb, L"%u", tag[126]);
            out.track = tb;
        }
    } else {
        take(out.comment, 97, 30);
    }
    if (out.genre.empty() && tag[127] != 255) {
        const wchar_t* g = GenreName(tag[127]);
        if (g) out.genre = g;
    }
}

// Skip an ID3v2 extended header. `p` points at it, returns bytes to skip.
size_t SkipExtHeader(const unsigned char* p, size_t avail, int verMajor) {
    if (verMajor == 3) {
        if (avail < 4) return 0;
        return (size_t)BE32(p); // size excludes these 4 bytes
    }
    if (verMajor == 4) {
        if (avail < 4) return 0;
        return (size_t)SyncSafe(p); // size includes everything
    }
    return 0; // v2.2 has no extended header
}

const char* MapFrameId(const char* id4, int verMajor, char v22id[4]) {
    if (verMajor == 2) {
        // v2.2 uses 3-char ids; normalize for the same switch below.
        if (memcmp(id4, "TT2", 3) == 0) return "TIT2";
        if (memcmp(id4, "TP1", 3) == 0) return "TPE1";
        if (memcmp(id4, "TAL", 3) == 0) return "TALB";
        if (memcmp(id4, "TRK", 3) == 0) return "TRCK";
        if (memcmp(id4, "TYE", 3) == 0) return "TYER";
        if (memcmp(id4, "TCO", 3) == 0) return "TCON";
        if (memcmp(id4, "COM", 3) == 0) return "COMM";
        (void)v22id;
        return nullptr;
    }
    static char id[5];
    memcpy(id, id4, 4);
    id[4] = 0;
    return id;
}

void ReadV2(const unsigned char* data, size_t total, ID3Tags& out) {
    if (total < 10) return;
    if (memcmp(data, "ID3", 3) != 0) return;
    int verMajor = data[3];
    if (verMajor < 2 || verMajor > 4) return;
    unsigned char flags = data[5];
    unsigned tagSize = SyncSafe(data + 6);
    if (tagSize + 10 > total) tagSize = (unsigned)(total - 10);

    // Copy tag body, de-unsynchronising if flagged.
    std::vector<unsigned char> body;
    body.reserve(tagSize);
    bool unsync = (flags & 0x80) != 0;
    const unsigned char* src = data + 10;
    for (unsigned i = 0; i < tagSize; i++) {
        body.push_back(src[i]);
        if (unsync && src[i] == 0xFF && i + 1 < tagSize && src[i + 1] == 0x00) i++;
    }

    size_t pos = 0;
    if (flags & 0x40) { // extended header present
        size_t skip = SkipExtHeader(body.data(), body.size(), verMajor);
        if (verMajor == 4) pos = skip;          // v2.4 size includes itself
        else pos = 4 + skip;                    // v2.3 size excludes the 4 size bytes
        if (pos > body.size()) return;
    }

    auto take = [&](std::wstring& dst, const std::wstring& v) {
        if (!v.empty() && dst.empty()) dst = v;
    };

    while (pos + (verMajor == 2 ? 6u : 10u) <= body.size()) {
        char id4[4] = {0, 0, 0, 0};
        unsigned frameSize = 0;
        size_t headerLen = 0;
        if (verMajor == 2) {
            memcpy(id4, body.data() + pos, 3);
            if (id4[0] == 0) break; // padding
            frameSize = ((unsigned)body[pos + 3] << 16) |
                        ((unsigned)body[pos + 4] << 8) | (unsigned)body[pos + 5];
            headerLen = 6;
        } else {
            memcpy(id4, body.data() + pos, 4);
            if (id4[0] == 0) break; // padding
            if (verMajor == 4) frameSize = SyncSafe(body.data() + pos + 4);
            else frameSize = BE32(body.data() + pos + 4);
            headerLen = 10; // (skip 2 flag bytes as part of header)
        }
        pos += headerLen;
        if (frameSize == 0 || pos + frameSize > body.size()) {
            if (pos + frameSize > body.size()) break;
            continue;
        }
        const char* norm = MapFrameId(id4, verMajor, nullptr);
        if (norm) {
            const char* fdata = (const char*)body.data() + pos;
            if (memcmp(norm, "TIT2", 4) == 0) take(out.title, DecodeTextFrame(fdata, frameSize));
            else if (memcmp(norm, "TPE1", 4) == 0) take(out.artist, DecodeTextFrame(fdata, frameSize));
            else if (memcmp(norm, "TALB", 4) == 0) take(out.album, DecodeTextFrame(fdata, frameSize));
            else if (memcmp(norm, "TRCK", 4) == 0) {
                std::wstring v = DecodeTextFrame(fdata, frameSize);
                size_t slash = v.find(L'/'); // "4/12" -> "4"
                if (slash != std::wstring::npos) v.resize(slash);
                take(out.track, v);
            }
            else if (memcmp(norm, "TYER", 4) == 0 || memcmp(norm, "TDRC", 4) == 0)
                take(out.year, DecodeTextFrame(fdata, frameSize));
            else if (memcmp(norm, "TCON", 4) == 0) {
                std::wstring v = DecodeTextFrame(fdata, frameSize);
                if (!v.empty() && v[0] == L'(') {
                    // "(17)" genre index form
                    size_t end = v.find(L')');
                    if (end != std::wstring::npos) {
                        int idx = _wtoi(v.substr(1, end - 1).c_str());
                        const wchar_t* g = GenreName(idx);
                        if (g) { take(out.genre, g); pos += frameSize; continue; }
                    }
                }
                take(out.genre, v);
            }
            else if (memcmp(norm, "COMM", 4) == 0 && frameSize > 4) {
                // encoding(1) + language(3) + descriptor + text
                unsigned char enc = (unsigned char)fdata[0];
                size_t p = 4;
                if (enc == 0 || enc == 3) {
                    while (p < frameSize && fdata[p] != 0) p++;
                    p++; // skip descriptor NUL
                } else {
                    while (p + 1 < frameSize && !(fdata[p] == 0 && fdata[p + 1] == 0)) p += 2;
                    p += 2;
                }
                if (p < frameSize) {
                    // Re-decode text part with original encoding byte.
                    std::vector<char> tmp;
                    tmp.push_back(fdata[0]);
                    tmp.insert(tmp.end(), fdata + p, fdata + frameSize);
                    take(out.comment, DecodeTextFrame(tmp.data(), tmp.size()));
                }
            }
        }
        pos += frameSize;
    }
}

} // namespace

std::map<std::wstring, std::wstring> ID3Tags::fields() const {
    std::map<std::wstring, std::wstring> m;
    if (!title.empty()) m[L"Title"] = title;
    if (!artist.empty()) m[L"Artist"] = artist;
    if (!album.empty()) m[L"Album"] = album;
    if (!track.empty()) m[L"Track"] = track;
    if (!year.empty()) m[L"Year"] = year;
    if (!genre.empty()) m[L"Genre"] = genre;
    if (!comment.empty()) m[L"Comment"] = comment;
    return m;
}

bool ReadID3Tags(const wchar_t* path, ID3Tags& out) {
    out = ID3Tags();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return false;

    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        long long size = _ftelli64(f);
        if (size > 0) {
            ok = true;
            // v2 tag lives at the head (cap the read at 1 MB).
            unsigned char head[10];
            if (ReadAt(f, 0, head, 10) && memcmp(head, "ID3", 3) == 0 &&
                head[3] >= 2 && head[3] <= 4) {
                unsigned tagSize = SyncSafe(head + 6);
                if (tagSize > 1024 * 1024) tagSize = 1024 * 1024;
                std::vector<unsigned char> buf((size_t)tagSize + 10);
                if (ReadAt(f, 0, buf.data(), buf.size()))
                    ReadV2(buf.data(), buf.size(), out);
            }
            ReadV1(f, size, out); // v1 fills only fields v2 left empty
        }
    }
    fclose(f);
    return ok;
}
