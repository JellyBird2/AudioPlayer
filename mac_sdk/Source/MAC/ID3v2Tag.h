#pragma once

#include "All.h"

namespace APE
{

class CID3v2Tag : public APE::IID3v2Tag
{
public:
    CID3v2Tag();
    virtual ~CID3v2Tag() APE_OVERRIDE;

    // IID3v2Tag
    virtual bool Analyze(unsigned char * pTagData, int nTagBytes) APE_OVERRIDE;

    typedef enum { ISO_8859_1, UTF_16, UTF_16BE, UTF_8 } ID3v2TextEncoding;

    int m_nTagBytes;
    CSmartPtr<unsigned char> m_spTag;
    CSmartPtr<str_utfn> m_spTitle;
    CSmartPtr<str_utfn> m_spArtist;
    CSmartPtr<str_utfn> m_spAlbumArtist;
    CSmartPtr<str_utfn> m_spAlbum;
    CSmartPtr<str_utfn> m_spGenre;
    CSmartPtr<str_utfn> m_spComposer;
    CSmartPtr<str_utfn> m_spComment;
    CSmartPtr<str_utfn> m_spYear;
    CSmartPtr<str_utfn> m_spTrackNumber;
    CSmartPtr<str_utfn> m_spDiscNumber;
    CSmartPtr<str_utfn> m_spImageMimeType;
    CSmartPtr<unsigned char> m_spImage;
    int m_nImageBytes;

private:

    void ConvertID3v2String(CSmartPtr<str_utfn> * pTag, ID3v2TextEncoding Encoding, unsigned char * pStart, int * pnLengthBytes = APE_NULL, int nBytes = -1);
    void ReadTag(int64 z, uint32 nFrameBytes, CSmartPtr<str_utfn> * pTag);

    #pragma pack(push, 1)
    class CID3v2Header
    {
    public:
        uint8 m_aryID[3];
        uint8 m_nVersionMajor;
        uint8 m_nVersionMinor;
        uint8 m_nHeaderFlags;
        uint32 m_nTagBytes; // doesn't include header size
    };
    #pragma pack(pop)
};

}
