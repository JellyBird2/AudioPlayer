// ID3 - minimal reader for MP3 tags (ID3v1 footer + ID3v2.2/2.3/2.4 text frames).
// Defensive: anything unexpected just yields fewer fields, never an error.
#pragma once

#include <string>
#include <map>

struct ID3Tags {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring track;
    std::wstring year;
    std::wstring genre;
    std::wstring comment;
    bool empty() const {
        return title.empty() && artist.empty() && album.empty() &&
               track.empty() && year.empty() && genre.empty() && comment.empty();
    }
    // APE field name -> value, for tagging.
    std::map<std::wstring, std::wstring> fields() const;
};

// Reads tags from an MP3 file. Returns true if the file could be examined
// (even when no tags were found); fills `out` with whatever was found.
bool ReadID3Tags(const wchar_t* path, ID3Tags& out);
