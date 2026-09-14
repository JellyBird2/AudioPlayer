#include "stdafx.h"
#include "ID3v2Tag.h"
#include "GlobalFunctions.h"
#include "CharacterHelper.h"

namespace APE
{

CID3v2Tag::CID3v2Tag()
{
    m_bFoundTag = false;
    m_nTagBytes = 0;
    m_nImageBytes = 0;
}

CID3v2Tag::~CID3v2Tag()
{
    // added to make Clang happy
}

bool CID3v2Tag::Analyze(unsigned char * pTagData, int nTagBytes)
{
    // create the buffer and copy the tag
    m_nTagBytes = nTagBytes;

    // over allocate a little so we don't have to worry about buffer size as we read the data
    // we also zero the buffer so doing string length tests in the buffer will always be safe (string could be long, but won't ever be infinite)
    m_spTag.AllocateArray(static_cast<int64>(nTagBytes) + 1024, true);

    // copy tag data
    memcpy(m_spTag, pTagData, static_cast<size_t>(nTagBytes));

    // read header
    CID3v2Header * pHeader = reinterpret_cast<CID3v2Header *>(m_spTag.GetPtr());

    // check header
    bool bHeaderMatches = false;
    if ((m_spTag != APE_NULL) &&
        (pHeader->m_aryID[0] == 'I') &&
        (pHeader->m_aryID[1] == 'D') &&
        (pHeader->m_aryID[2] == '3'))
    {
        bHeaderMatches = true;
    }

    // check for unsupported flags
    if (bHeaderMatches)
    {
        // reading a tag footer should be fine
        #define ID3V2_HEADER_FLAG_FOOTER (1 << 4)
        if (pHeader->m_nHeaderFlags & ID3V2_HEADER_FLAG_FOOTER)
            bHeaderMatches = true; // should be supported

        // extended headers (and reading and using the size) is not currently supported
        #define ID3V2_HEADER_FLAG_EXTENDED_HEADER (1 << 6)
        if (pHeader->m_nHeaderFlags & ID3V2_HEADER_FLAG_EXTENDED_HEADER)
            bHeaderMatches = false;

        // we don't support unsynchronization
        #define ID3V2_HEADER_FLAG_UNSYNCHRONIZATION (1 << 7)
        if (pHeader->m_nHeaderFlags & ID3V2_HEADER_FLAG_UNSYNCHRONIZATION)
            bHeaderMatches = false;

        // we don't support experimental frames
        #define ID3V2_HEADER_FLAG_EXPERIMENTAL (1 << 5)
        if (pHeader->m_nHeaderFlags & ID3V2_HEADER_FLAG_EXPERIMENTAL)
            bHeaderMatches = false;
    }

    // check version
    typedef enum { ID3v23, ID3v24, Unknown } Versions;
    Versions Version = Unknown;
    if (bHeaderMatches)
    {
        if (pHeader->m_nVersionMajor == 3)
            Version = ID3v23;
        else if (pHeader->m_nVersionMajor == 4)
            Version = ID3v24;
    }

    // proceed if the file seems valid
    if (bHeaderMatches && ((Version == ID3v23) || (Version == ID3v24)))
    {
        // get size
        pHeader->m_nTagBytes = ConvertU32BE(pHeader->m_nTagBytes);
        pHeader->m_nTagBytes = (pHeader->m_nTagBytes & 0x0000007F) |
            ((pHeader->m_nTagBytes & 0x00007F00) >> 1) |
            ((pHeader->m_nTagBytes & 0x007F0000) >> 2) |
            ((pHeader->m_nTagBytes & 0x7F000000) >> 3);
        int nHeaderTagBytes = static_cast<int>(pHeader->m_nTagBytes) + ((pHeader->m_nHeaderFlags & ID3V2_HEADER_FLAG_FOOTER) ? 20 : 10);

        // start byte
        int nTagStartByte = 10; // skip the ID3 header stuff

        // tags often end with a string of NULL bytes so we'll check for that and not scan the buffer into the NULLs
        int nTailNull = 0;
        while (m_spTag[m_nTagBytes - 1 - nTailNull] == 0)
            nTailNull++;

        // update size
        int nTagBytesWithoutTailNull = m_nTagBytes - nTailNull;
        m_nTagBytes = APE_MIN(nTagBytesWithoutTailNull, nHeaderTagBytes);

        // loop the tag searching for frame markers
        int64 z = nTagStartByte;
        while (z < (m_nTagBytes - 8))
        {
            // get frame size
            uint32 nFrameBytes = *(reinterpret_cast<uint32 *>(&m_spTag[z + 4]));

            // get frame bytes
            nFrameBytes = ConvertU32BE(nFrameBytes);

            // we're a sync-safe integer if we're ID3v2.4
            if (Version == ID3v24)
            {
                nFrameBytes = (nFrameBytes & 0x0000007F) |
                    ((nFrameBytes & 0x00007F00) >> 1) |
                    ((nFrameBytes & 0x007F0000) >> 2) |
                    ((nFrameBytes & 0x7F000000) >> 3);
            }

            CSmartPtr<str_utfn> * pTag = APE_NULL;
            if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'I') && (m_spTag[z + 2] == 'T') && (m_spTag[z + 3] == '2'))
                pTag = &m_spTitle;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'P') && (m_spTag[z + 2] == 'E') && (m_spTag[z + 3] == '1'))
                pTag = &m_spArtist;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'P') && (m_spTag[z + 2] == 'E') && (m_spTag[z + 3] == '2'))
                pTag = &m_spAlbumArtist;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'A') && (m_spTag[z + 2] == 'L') && (m_spTag[z + 3] == 'B'))
                pTag = &m_spAlbum;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'Y') && (m_spTag[z + 2] == 'E') && (m_spTag[z + 3] == 'R'))
                pTag = &m_spYear;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'C') && (m_spTag[z + 2] == 'O') && (m_spTag[z + 3] == 'N'))
                pTag = &m_spGenre;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'C') && (m_spTag[z + 2] == 'O') && (m_spTag[z + 3] == 'M'))
                pTag = &m_spComposer;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'R') && (m_spTag[z + 2] == 'C') && (m_spTag[z + 3] == 'K'))
                pTag = &m_spTrackNumber;
            else if ((m_spTag[z + 0] == 'T') && (m_spTag[z + 1] == 'P') && (m_spTag[z + 2] == 'O') && (m_spTag[z + 3] == 'S'))
                pTag = &m_spDiscNumber;

            if ((pTag != APE_NULL) && (pTag->GetPtr() == APE_NULL))
            {
                // found a tag
                ReadTag(z, nFrameBytes, pTag);
            }
            else if ((m_spTag[z + 0] == 'A') && (m_spTag[z + 1] == 'P') && (m_spTag[z + 2] == 'I') && (m_spTag[z + 3] == 'C') && (m_spImage == APE_NULL))
            {
                // found an image frame (APIC)
                char * pMime = reinterpret_cast<char *>(&m_spTag[z + 11]);
                int nMime = static_cast<int>(strlen(pMime));
                char * pDescription = reinterpret_cast<char *>(&m_spTag[z + 11 + nMime + 2]);
                int nDescription = static_cast<int>(strlen(pDescription));

                // MIME type
                m_spImageMimeType.Assign(CAPECharacterHelper::GetUTFNFromUTF8(reinterpret_cast<str_utf8 *>(pMime)), true);

                // the image bytes left
                int nImageBytesLeft = static_cast<int>(nFrameBytes) - static_cast<int>(nMime) - nDescription - 4;

                // image data
                m_nImageBytes = nImageBytesLeft;
                m_spImage.AllocateArray(m_nImageBytes);
                memcpy(m_spImage.GetPtr(), &m_spTag[z + 11 + nMime + 2 + nDescription + 1], static_cast<size_t>(m_nImageBytes));
            }
            else if ((m_spTag[z + 0] == 'C') && (m_spTag[z + 1] == 'O') && (m_spTag[z + 2] == 'M') && (m_spTag[z + 3] == 'M'))
            {
                // comments are a little dense, but we just decode two strings (description then value) and use the second string decoded

                // read encoding and get the start byte
                int nEncoding = m_spTag[z + 10];

                // language bytes 11, 12, 13

                // short description
                unsigned char * pStart = &m_spTag[z + 14];

                // decode
                ID3v2TextEncoding Encoding = static_cast<ID3v2TextEncoding>(nEncoding);

                int nDescriptionBytes = 0;
                CSmartPtr<str_utfn> spCommentDescription;
                ConvertID3v2String(&spCommentDescription, Encoding, pStart, &nDescriptionBytes, -1); // this string must be NULL terminated because we need to get the proper length to read the next string

                if (_wcsicmp(spCommentDescription, _T("MusicMatch_Preference")) == 0)
                {
                    // eat this because it's worthless
                }
                else
                {
                    int nValueBytes = static_cast<int>(nFrameBytes) - nDescriptionBytes - 4; // back off encoding, language, description, etc.

                    CSmartPtr<str_utfn> spCommentValue;
                    ConvertID3v2String(&spCommentValue, Encoding, pStart + nDescriptionBytes, APE_NULL, nValueBytes);

                    if (m_spComment == APE_NULL)
                    {
                        // no comment yet, so use this string
                        m_spComment.Assign(spCommentValue.GetPtr(), true);
                        spCommentValue.SetDelete(false);
                    }
                    else
                    {
                        // append this string with a semi-colon between the values
                        CSmartPtr<str_utfn> spCommentFull;
                        size_t nCharacters = wcslen(m_spComment) + 1 + wcslen(spCommentValue) + 1;
                        spCommentFull.AllocateArray(static_cast<int64>(nCharacters));
                        wcscpy_s(spCommentFull, nCharacters, m_spComment);
                        wcscat_s(spCommentFull, nCharacters, L";");
                        wcscat_s(spCommentFull, nCharacters, spCommentValue);
                        m_spComment.Assign(spCommentFull.GetPtr(), true);
                        spCommentFull.SetDelete(false);
                    }
                }
            }

            // skip to the next tag (back off one since the loop increments)
            z += static_cast<int64>(nFrameBytes) + 10; // skip this frame
        }

        // we found a tag
        m_bFoundTag = true;
    }

    return m_bFoundTag;
}

void CID3v2Tag::ReadTag(int64 z, uint32 nFrameBytes, CSmartPtr<str_utfn> * pTag)
{
    // found a tag
    int nEncoding = m_spTag[z + 10];
    unsigned char * pStart = &m_spTag[z + 11];

    // get size of value
    int nValueBytes = static_cast<int>(nFrameBytes) - 1; // back off encoding

    // decode
    ID3v2TextEncoding Encoding = static_cast<ID3v2TextEncoding>(nEncoding);
    ConvertID3v2String(pTag, Encoding, pStart, APE_NULL, nValueBytes);
}

void CID3v2Tag::ConvertID3v2String(CSmartPtr<str_utfn> * pTag, ID3v2TextEncoding Encoding, unsigned char * pStart, int * pnLengthBytes, int nBytes)
{
    if (Encoding == ISO_8859_1)
    {
        int nLength = (nBytes >= 0) ? nBytes : static_cast<int>(strlen(reinterpret_cast<char*>(pStart)));
        CSmartPtr<str_ansi> spTagValueANSI(new str_ansi[static_cast<size_t>(nLength + 1)], true);
        APE_CLEAR_ARRAY(spTagValueANSI, nLength + 1);
        memcpy(spTagValueANSI.GetPtr(), reinterpret_cast<char *>(pStart), static_cast<size_t>(nLength));
        pTag->Assign(CAPECharacterHelper::GetUTFNFromANSI(spTagValueANSI.GetPtr()), true);
        if (pnLengthBytes) *pnLengthBytes = nLength + 1;
    }
    else if (Encoding == UTF_8)
    {
        int nLength = (nBytes >= 0) ? nBytes : static_cast<int>(strlen(reinterpret_cast<char*>(pStart)));
        CSmartPtr<str_utf8> spTagValueUTF8(new str_utf8[static_cast<size_t>(nLength + 1)], true);
        APE_CLEAR_ARRAY(spTagValueUTF8, nLength + 1);
        memcpy(spTagValueUTF8.GetPtr(), reinterpret_cast<char*>(pStart), static_cast<size_t>(nLength));
        pTag->Assign(CAPECharacterHelper::GetUTFNFromUTF8(spTagValueUTF8.GetPtr()), true);
        if (pnLengthBytes) *pnLengthBytes = nLength + 1;
    }
    else if ((Encoding == UTF_16) && (sizeof(str_utfn) == 2))
    {
        str_utfn * pString = reinterpret_cast<str_utfn *>(pStart);
        bool bUTFMarker = (pString[0] == 0xFEFF);
        if (bUTFMarker)
        {
            pStart += 2;
            pString = reinterpret_cast<str_utfn *>(pStart);
            nBytes -= 2;
        }
        int nLength = (nBytes >= 0) ? (nBytes / static_cast<int>(sizeof(str_utfn))) : static_cast<int>(wcslen(pString));
        CSmartPtr<str_utfn> spTagValueUTFN(new str_utfn[static_cast<size_t>(nLength + 1)], true);
        APE_CLEAR_ARRAY(spTagValueUTFN, nLength + 1);
        memcpy(spTagValueUTFN.GetPtr(), pStart, static_cast<size_t>(nLength) * sizeof(str_utfn));
        pTag->Assign(spTagValueUTFN, true);
        spTagValueUTFN.SetDelete(false);
        if (pnLengthBytes) *pnLengthBytes = (bUTFMarker ? 2 : 0) + (nLength + 1) * static_cast<int>(sizeof(str_utfn));
    }
    else if ((Encoding == UTF_16BE) && (sizeof(str_utfn) == 2))
    {
        str_utfn * pString = reinterpret_cast<str_utfn*>(pStart);
        bool bUTFMarker = (pString[0] == 0xFFFE);
        if (bUTFMarker)
        {
            pStart += 2;
            pString = reinterpret_cast<str_utfn*>(pStart);
            nBytes -= 2;
        }
        int nLength = (nBytes >= 0) ? (nBytes / static_cast<int>(sizeof(str_utfn))) : static_cast<int>(wcslen(pString));
        CSmartPtr<str_utfn> spTagValueUTFN(new str_utfn[static_cast<size_t>(nLength + 1)], true);
        APE_CLEAR_ARRAY(spTagValueUTFN, nLength + 1);
        memcpy(spTagValueUTFN.GetPtr(), pStart, static_cast<size_t>(nLength) * sizeof(str_utfn));
        SwitchBufferBytes(spTagValueUTFN.GetPtr(), 2, nLength);
        pTag->Assign(spTagValueUTFN, true);
        spTagValueUTFN.SetDelete(false);
        if (pnLengthBytes) *pnLengthBytes = (bUTFMarker ? 2 : 0) + (nLength + 1) * static_cast<int>(sizeof(str_utfn));
    }
}
}
