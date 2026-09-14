/**************************************************************************************************
MAC Console Frontend (MAC.exe)

Pretty simple and straightforward console front end.
**************************************************************************************************/
#include "Console.h"
#include <stdio.h>
#include "GlobalFunctions.h"
#include "MACLib.h"
#include "APETag.h"
#include "APEInfo.h"
#include "CharacterHelper.h"
#include "APETicks.h"
#ifdef PLATFORM_WINDOWS
    #include <iostream>
    #include <io.h>
    #include <fcntl.h>
#endif
using namespace APE;

// defines
#define COMPRESS_MODE               (0)
#define DECOMPRESS_MODE             (1)
#define VERIFY_MODE                 (2)
#define CONVERT_MODE                (3)
#define TAG_MODE                    (4)
#define VERIFY_FULL_MODE            (5)
#define REMOVE_TAG_MODE             (6)
#define CONVERT_TAG_TO_LEGACY_MODE  (7)
#define TAG_DUMP_MODE               (8)
#define UNDEFINED_MODE              (-1)

// global variables
static CAPEDuration g_ProcessingDuration;

/**************************************************************************************************
Data holder for the arguments so we always have the argument count
**************************************************************************************************/
class CArguments
{
public:
#ifndef PLATFORM_WINDOWS
    CArguments(int argc, char ** argv)
#else
    CArguments(int argc, TCHAR ** argv)
#endif
    {
        m_argv = argv;
        m_argc = argc;
    }

    // data
#ifndef PLATFORM_WINDOWS
    char ** m_argv;
#else
    TCHAR ** m_argv;
#endif
    int m_argc;

    /**************************************************************************************************
    Get an argument
    **************************************************************************************************/
    void GetArgument(int nArgument, APE::CSmartPtr<APE::str_utfn> & rspString) const
    {
        if ((nArgument >= 0) && (nArgument < m_argc))
        {
            #ifdef PLATFORM_WINDOWS
                #ifdef _UNICODE
                    // make a copy of the string
                    //rspString.Assign(new APE::str_utfn [wcslen(m_argv[nArgument]) + 1], true);
                    //wcscpy_s(rspString, wcslen(m_argv[nArgument]) + 1, m_argv[nArgument]);

                    // simply return the string from the buffer and tell the pointer not to delete
                    rspString.Assign(m_argv[nArgument], true, false);
                #else
                    // convert from ANSI
                    rspString.Assign(CAPECharacterHelper::GetUTFNFromANSI(m_argv[nArgument]), true);
                #endif
            #else
                // convert from UTF-8
                rspString.Assign(CAPECharacterHelper::GetUTFNFromUTF8((str_utf8 *) m_argv[nArgument]), true);
            #endif
        }
        else
        {
            // undefined so we'll just return NULL
            rspString.Delete();
        }
    }

    /**************************************************************************************************
    Get arguments
    **************************************************************************************************/
    void GetOptions(int & rnMode, int & rnCompressionLevel, int & rnThreads, bool & rbReadFullInput, APE::CSmartPtr<APE::str_utfn> & rspTagSwitch, APE::CSmartPtr<APE::str_utfn> & rspInputFilename, APE::CSmartPtr<APE::str_utfn> & rspOutputFilename) const
    {
        // defaults (these are already set by the caller, but just here for good form)
        rnMode = UNDEFINED_MODE;
        rnCompressionLevel = APE_COMPRESSION_LEVEL_NORMAL;
        rnThreads = 1;
        rbReadFullInput = false;
        rspTagSwitch.Delete();
        rspInputFilename.Delete();
        rspOutputFilename.Delete();

        // load
        for (int z = 1; ; z++)
        {
            // get the argument
            CSmartPtr<str_utfn> spOption;
            GetArgument(z, spOption);
            if (spOption == APE_NULL)
                break; // we reached the end

            // check for known arguments
            // put full words before short stuff like -r since that can also be in the full words
            if (_wcsnicmp(spOption, L"-threads=", 9) == 0)
            {
                rnThreads = _wtoi(&spOption[9]);
            }
            else if (_wcsnicmp(spOption, L"-readfullinput", 14) == 0)
            {
                rbReadFullInput = true;
            }
            else if (_wcsnicmp(spOption, L"-tagdump", 8) == 0)
            {
                rnMode = TAG_DUMP_MODE;
            }
            else if (_wcsnicmp(spOption, L"-c", 2) == 0)
            {
                rnMode = COMPRESS_MODE;
                rnCompressionLevel = _wtoi(&spOption[2]);
            }
            else if (_wcsnicmp(spOption, L"-d", 2) == 0)
            {
                rnMode = DECOMPRESS_MODE;
            }
            else if (wcsncmp(spOption, L"-V", 2) == 0)
            {
                rnMode = VERIFY_FULL_MODE;
            }
            else if (wcsncmp(spOption, L"-v", 2) == 0)
            {
                rnMode = VERIFY_MODE;
            }
            else if (_wcsnicmp(spOption, L"-n", 2) == 0)
            {
                rnMode = CONVERT_MODE;
                rnCompressionLevel = _wtoi(&spOption[2]);
            }
            else if (_wcsnicmp(spOption, L"-r", 2) == 0)
            {
                rnMode = REMOVE_TAG_MODE;
            }
            else if (_wcsnicmp(spOption, L"-t", 2) == 0)
            {
                if (rnMode == UNDEFINED_MODE)
                    rnMode = TAG_MODE;
                GetArgument(z + 1, rspTagSwitch);
                z++;
            }
            else if (_wcsnicmp(spOption, L"-L", 2) == 0)
            {
                rnMode = CONVERT_TAG_TO_LEGACY_MODE;
            }
            else if (rspInputFilename == APE_NULL)
            {
                GetArgument(z, rspInputFilename);
            }
            else if (rspOutputFilename == APE_NULL)
            {
                GetArgument(z, rspOutputFilename);
            }
        }

        // validate
        if ((rspInputFilename != APE_NULL) && (FileExists(rspInputFilename) == false))
        {
            rspInputFilename.Delete();
        }
    }
};

/**************************************************************************************************
Gets a parameter from an array
**************************************************************************************************/
static str_utfn * GetParameterFromList(const str_utfn * pList, const str_utfn * pDelimiter, int nCount)
{
    const str_utfn * pHead = pList;
    const str_utfn * pTail = wcsstr(pHead, pDelimiter);
    while (pTail != APE_NULL)
    {
        int nBufferSize = static_cast<int>(pTail - pHead);
        str_utfn * pBuffer = new str_utfn [static_cast<size_t>(nBufferSize) + 1];
        memcpy(pBuffer, pHead, static_cast<size_t>(nBufferSize) * sizeof(pBuffer[0]));
        pBuffer[nBufferSize] = 0;

        pHead = pTail + wcslen(pDelimiter);
        pTail = wcsstr(pHead, pDelimiter);

        nCount--;
        if (nCount < 0)
            return pBuffer;

        delete [] pBuffer;
    }

    if ((*pHead != 0) && (nCount == 0))
    {
        int nBufferSize = static_cast<int>(wcslen(pHead));
        str_utfn * pBuffer = new str_utfn [static_cast<size_t>(nBufferSize) + 1];
        memcpy(pBuffer, pHead, static_cast<size_t>(nBufferSize) * sizeof(pBuffer[0]));
        pBuffer[nBufferSize] = 0;
        return pBuffer;
    }

    return APE_NULL;
}

/**************************************************************************************************
Displays the proper usage for MAC.exe
**************************************************************************************************/
static void DisplayProperUsage(FILE * pFile)
{
    fwprintf(pFile, L"Proper Usage: [EXE] [Input File] [Output File] [Mode]\n\n");

    fwprintf(pFile, L"Modes:\n");
    fwprintf(pFile, L"    Compress (fast): '-c1000'\n");
    fwprintf(pFile, L"    Compress (normal): '-c2000'\n");
    fwprintf(pFile, L"    Compress (high): '-c3000'\n");
    fwprintf(pFile, L"    Compress (extra high): '-c4000'\n");
    fwprintf(pFile, L"    Compress (insane): '-c5000'\n");
    fwprintf(pFile, L"    Decompress: '-d'\n");
    fwprintf(pFile, L"    Verify: '-v'\n");
    fwprintf(pFile, L"    Full Verify: '-V'\n");
    fwprintf(pFile, L"    Convert: '-nXXXX'\n");
    fwprintf(pFile, L"    Tag: '-t'\n");
    fwprintf(pFile, L"    Remove Tag: '-r'\n");
    fwprintf(pFile, L"    Convert to ID3v1 (needed by some players, etc.): '-L'\n\n");

    fwprintf(pFile, L"Options:\n");
    fwprintf(pFile, L"    Set the number of threads when compressing or decompressing: -threads=#'\n\n");
    fwprintf(pFile, L"    Read the entire file until the end of file if it's unknown length so the seek table can be made perfect: -readfullinput'\n\n");

    fwprintf(pFile, L"Examples:\n");
    fwprintf(pFile, L"    Compress: mac.exe \"Metallica - One.wav\" \"Metallica - One.ape\" -c2000\n");
    fwprintf(pFile, L"    Compress: mac.exe \"Metallica - One.wav\" \"Metallica - One.ape\" -c2000 -threads=16 -t \"Artist=Metallica|Album=Black|Name=One\"\n");
    fwprintf(pFile, L"    Compress: mac.exe \"Metallica - One.wav\" \"Metallica - One.ape\" -c2000 -t \"Artist=Metallica|Album=Black|Name=One\"\n");
    fwprintf(pFile, L"    Compress: mac.exe \"Metallica - One.wav\" auto -c2000\n");
    fwprintf(pFile, L"    Transcode from pipe: ffmpeg.exe -i \"Metallica - One.flac\" -f wav - | mac.exe - \"Metallica - One.ape\" -c2000\n");
    fwprintf(pFile, L"    Decompress: mac.exe \"Metallica - One.ape\" \"Metallica - One.wav\" -d\n");
    fwprintf(pFile, L"    Decompress: mac.exe \"Metallica - One.ape\" auto -d\n");
    fwprintf(pFile, L"    Decompress: mac.exe \"Metallica - One.ape\" \"Metallica - One.wav\" -d -threads=16\n");
    fwprintf(pFile, L"    Verify: mac.exe \"Metallica - One.ape\" -v\n");
    fwprintf(pFile, L"    Full Verify: mac.exe \"Metallica - One.ape\" -V\n");
    fwprintf(pFile, L"    Tag: mac.exe \"Metallica - One.ape\" -t \"Artist=Metallica|Album=Black|Name=One|Comment=\\\"This is in quotes\\\"\"\n");
    fwprintf(pFile, L"    Remove tag: mac.exe \"Metallica - One.ape\" -r\n");
}

/**************************************************************************************************
Progress callback
**************************************************************************************************/
class CConsoleProgressCallback : public IAPEProgressCallback
{
public:
    virtual void Progress(int nPercentageDone) APE_OVERRIDE
    {
        // calculate the progress
        double dProgress = nPercentageDone / 1.e5;                                                    // [0...1]
        double dElapsed = g_ProcessingDuration.GetElapsedSeconds();                                   // seconds
        double dRemaining = (nPercentageDone > 0) ? dElapsed * ((1.0 / dProgress) - 1.0) : 0;         // seconds

        // output the progress
        fwprintf(stderr, L"Progress: %.1f%% (%.1f seconds remaining, %.1f seconds total)          \r",
            dProgress * 100, dRemaining, dElapsed);

        // don't forget to flush!
        fflush(stderr);

        // update the title
        #ifdef PLATFORM_WINDOWS
            wchar_t cTitle[1024];
            swprintf_s(cTitle, 1024, L"%.1f%% - Monkey's Audio", dProgress * 100);
            SetConsoleTitleW(cTitle);
        #endif
    }

    virtual int GetKillFlag() APE_OVERRIDE
    {
        return APE_KILL_FLAG_CONTINUE;
    }
};

static CConsoleProgressCallback s_Callback;
static CAPEProgressCallbackInfo s_CallbackInfo = { &s_Callback, APE_NULL, APE_NULL, APE_NULL };

/**************************************************************************************************
CtrlHandler callback
**************************************************************************************************/
#ifdef PLATFORM_WINDOWS
static BOOL CALLBACK CtrlHandlerCallback(DWORD dwCtrlTyp)
{
    switch (dwCtrlTyp)
    {
    case CTRL_C_EVENT:
        fputws(L"\n\nCtrl+C: MAC has been interrupted !!!\n", stderr);
        break;
    case CTRL_BREAK_EVENT:
        fputws(L"\n\nBreak: MAC has been interrupted !!!\n", stderr);
        break;
    default:
        return false;
    }

    fflush(stderr);
    ExitProcess(666);
}
#endif

/**************************************************************************************************
Tag
**************************************************************************************************/
static int Tag(const str_utfn * pFilename, const str_utfn * pTagString)
{
    // check for NULL
    if ((pFilename == APE_NULL) || (pTagString == APE_NULL))
        return ERROR_INVALID_FUNCTION_PARAMETER;

    int nRetVal = ERROR_UNDEFINED;

    // create the decoder
    int nFunctionRetVal = ERROR_SUCCESS;
    CSmartPtr<IAPEDecompress> spAPEDecompress;
    try
    {
        spAPEDecompress.Assign(CreateIAPEDecompress(pFilename, &nFunctionRetVal, false, true, false));
        if (spAPEDecompress == APE_NULL || nFunctionRetVal != ERROR_SUCCESS) throw(static_cast<intn>(nFunctionRetVal));

        // get the input format
        APE::IAPETag * pTag = APE_NULL;
        pTag = spAPEDecompress->GetTag();

        if (pTag == APE_NULL)
            throw(ERROR_UNDEFINED);

        fwprintf(stderr, L"%s\r\n", pTagString);

        // set fields
        for (int nCount = 0; true; nCount++)
        {
            CSmartPtr<str_utfn> spParameter(GetParameterFromList(pTagString, L"|", nCount), true);
            if (spParameter == APE_NULL)
                break;

            str_utfn * pEqual = wcsstr(spParameter, L"=");
            if (pEqual != APE_NULL)
            {
                int nCharacters = static_cast<int>(pEqual - spParameter);
                CSmartPtr<str_utfn> spLeft(new str_utfn [static_cast<size_t>(nCharacters) + 1], true);
                wcsncpy_s(spLeft, static_cast<size_t>(nCharacters) + 1, spParameter, static_cast<size_t>(nCharacters));
                spLeft[nCharacters] = 0;

                nCharacters = static_cast<int>(wcslen(&pEqual[1]));
                CSmartPtr<str_utfn> spRight(new str_utfn [static_cast<size_t>(nCharacters) + 1], true);
                wcscpy_s(spRight, static_cast<size_t>(nCharacters) + 1, &pEqual[1]);
                spRight[nCharacters] = 0;

                fwprintf(stderr, L"%s -> %s\r\n", spLeft.GetPtr(), spRight.GetPtr());

                if (_wcsicmp(spLeft, L"cover") == 0)
                {
                    // open the file
                    APE::CSmartPtr<IAPEIO> spIO(CreateIAPEIO());
                    if (spIO->Open(spRight) == ERROR_SUCCESS)
                    {
                        // get the extension
                        const str_utfn * pExtension = &spRight[wcslen(spRight)];
                        while ((pExtension > spRight) && (*pExtension != '.'))
                            pExtension--;

                        // read the file
                        size_t nBufferBytes = wcslen(pExtension) + 1 + static_cast<size_t>(spIO->GetSize());
                        APE::CSmartPtr<char> spBuffer(new char [nBufferBytes], true);

                        // copy extension (and NULL terminate)
                        for (int z = 0; z < static_cast<int>(wcslen(pExtension)) + 1; z++)
                            spBuffer[z] = static_cast<char>(pExtension[z]);

                        // read data
                        spIO->Read(&spBuffer[wcslen(pExtension) + 1], spIO->GetSize());

                        // set
                        pTag->SetFieldBinary(APE_TAG_FIELD_COVER_ART_FRONT, spBuffer, static_cast<intn>(nBufferBytes), TAG_FIELD_FLAG_DATA_TYPE_BINARY);
                    }

                }
                else
                {
                    pTag->SetFieldString(spLeft, spRight);
                }
            }
        }

        // save
        if (pTag->Save(false) == ERROR_SUCCESS)
        {
            nRetVal = ERROR_SUCCESS;
        }
    }
    catch (...)
    {
        nRetVal = ERROR_UNDEFINED;
    }

    return nRetVal;
}

/**************************************************************************************************
Remove tags
**************************************************************************************************/
static int RemoveTags(const str_utfn * pFilename)
{
    int nRetVal = ERROR_UNDEFINED;

    // create the decoder
    int nFunctionRetVal = ERROR_SUCCESS;
    CSmartPtr<IAPEDecompress> spAPEDecompress;
    try
    {
        spAPEDecompress.Assign(CreateIAPEDecompress(pFilename, &nFunctionRetVal, false, true, false));
        if (spAPEDecompress == APE_NULL || nFunctionRetVal != ERROR_SUCCESS) throw(static_cast<intn>(nFunctionRetVal));

        // get the input format
        APE::IAPETag * pTag = spAPEDecompress->GetTag();
        if (pTag == APE_NULL)
            throw(ERROR_UNDEFINED);

        // remove the tag
        if (pTag->Remove(true) == ERROR_SUCCESS)
        {
            nRetVal = ERROR_SUCCESS;
        }
    }
    catch (...)
    {
        nRetVal = ERROR_UNDEFINED;
    }

    return nRetVal;
}

/**************************************************************************************************
Convert to legacy
**************************************************************************************************/
static int ConvertTagsToLegacy(const str_utfn * pFilename)
{
    int nRetVal = ERROR_UNDEFINED;

    // create the decoder
    int nFunctionRetVal = ERROR_SUCCESS;
    CSmartPtr<IAPEDecompress> spAPEDecompress;
    try
    {
        spAPEDecompress.Assign(CreateIAPEDecompress(pFilename, &nFunctionRetVal, false, true, false));
        if (spAPEDecompress == APE_NULL || nFunctionRetVal != ERROR_SUCCESS) throw(static_cast<intn>(nFunctionRetVal));

        // get the input format
        APE::IAPETag * pTag = spAPEDecompress->GetTag();
        if (pTag == APE_NULL)
            throw(static_cast<intn>(ERROR_UNDEFINED));

        if (pTag->GetHasID3Tag())
        {
            // already done
            nRetVal = ERROR_SUCCESS;
        }
        else if (pTag->Save(true) == ERROR_SUCCESS)
        {
            // converted the tag
            nRetVal = ERROR_SUCCESS;
        }
    }
    catch (...)
    {
        nRetVal = ERROR_UNDEFINED;
    }

    return nRetVal;
}

/**************************************************************************************************
Convert to legacy
**************************************************************************************************/
static int TagDump(const str_utfn * pFilename)
{
    int nRetVal = ERROR_UNDEFINED;

    // create the decoder
    int nFunctionRetVal = ERROR_SUCCESS;
    CSmartPtr<IAPEDecompress> spAPEDecompress;
    try
    {
        spAPEDecompress.Assign(CreateIAPEDecompress(pFilename, &nFunctionRetVal, false, true, false));
        if (spAPEDecompress == APE_NULL || nFunctionRetVal != ERROR_SUCCESS) throw(static_cast<intn>(nFunctionRetVal));

        // get the input format
        APE::IAPETag * pTag = spAPEDecompress->GetTag();
        if (pTag == APE_NULL)
            throw(static_cast<intn>(ERROR_UNDEFINED));

        // dump tag
        str_utfn cTag[4096];
        APE_CLEAR(cTag);
        pTag->OutputTag(cTag, 4096);

        // output
        fwprintf(stderr, L"%s", cTag);

        // success
        nRetVal = ERROR_SUCCESS;
    }
    catch (...)
    {
        nRetVal = ERROR_UNDEFINED;
    }

    return nRetVal;
}

/**************************************************************************************************
Handle auto for an output filename
**************************************************************************************************/
static void HandleAuto(CSmartPtr<str_utfn> & rspInputFilename, CSmartPtr<str_utfn> & rspOutputFilename, bool bAPEType)
{
    // build the output filename
    const str_utfn * AUTO = L"auto";
    if ((rspOutputFilename == APE_NULL) || (wcslen(rspOutputFilename) == 0) || (_wcsicmp(rspOutputFilename, AUTO) == 0))
    {
        // build the output filename
        rspOutputFilename.AllocateArray(APE_MAX_PATH);
        wcscpy_s(rspOutputFilename, APE_MAX_PATH, rspInputFilename);

        // remove the extension
        str_utfn * pExtension = wcsrchr(rspOutputFilename, '.');
        if (pExtension != APE_NULL)
            *pExtension = 0; // switch to NULL so we don't have an extension

        // put together and fill spOutputFilename
        if (bAPEType)
        {
            // add the .ape extension
            wcscat_s(rspOutputFilename, APE_MAX_PATH, L".ape");
        }
        else
        {
            // add the new extension
            APE::str_ansi cFileType[8] = { 0 };
            GetAPEFileType(rspInputFilename, cFileType);
            CSmartPtr<APE::str_utfn> spFileTypeUTFN(CAPECharacterHelper::GetUTFNFromANSI(cFileType), true);

            // put together and fill spOutputFilename
            wcscat_s(rspOutputFilename, APE_MAX_PATH, spFileTypeUTFN);
        }
    }
}

/**************************************************************************************************
Main (the main function)
**************************************************************************************************/
#ifndef PLATFORM_WINDOWS
int main(int argc, char * argv[])
#else
int _tmain(int argc, TCHAR * argv[])
#endif
{
    // variable declares
    int nRetVal = ERROR_UNDEFINED;

    // initialize
    #ifdef PLATFORM_WINDOWS
        SetErrorMode(SetErrorMode(0x0003) | 0x0003);
        SetConsoleCtrlHandler(CtrlHandlerCallback, true);
        (void)_setmode(_fileno(stderr), _O_U16TEXT);
    #endif

    // arguments
    CArguments Arguments(argc, argv);

    // output the header
    fwprintf(stderr, CONSOLE_NAME);

    // make sure there are at least four arguments
    if (argc < 3)
    {
        DisplayProperUsage(stderr);
        exit(-1);
    }

    // get the arguments
    int nMode = UNDEFINED_MODE;
    int nCompressionLevel = APE_COMPRESSION_LEVEL_NORMAL;
    int nThreads = 1;
    bool bReadFullInput = false;
    APE::CSmartPtr<APE::str_utfn> spTagSwitch;
    APE::CSmartPtr<APE::str_utfn> spInputFilename;
    APE::CSmartPtr<APE::str_utfn> spOutputFilename;
    Arguments.GetOptions(nMode, nCompressionLevel, nThreads, bReadFullInput, spTagSwitch, spInputFilename, spOutputFilename);

    // verify that the input file exists (we check existence when we parse the options)
    if (spInputFilename == APE_NULL)
    {
        fwprintf(stderr, L"Input file not found...\n\n");
        exit(-1);
    }

    // error check the mode
    if (nMode == UNDEFINED_MODE)
    {
        DisplayProperUsage(stderr);
        exit(-1);
    }

    // get and error check the compression level
    if (nMode == COMPRESS_MODE || nMode == CONVERT_MODE)
    {
        if (nCompressionLevel != APE_COMPRESSION_LEVEL_FAST &&
            nCompressionLevel != APE_COMPRESSION_LEVEL_NORMAL &&
            nCompressionLevel != APE_COMPRESSION_LEVEL_HIGH &&
            nCompressionLevel != APE_COMPRESSION_LEVEL_EXTRA_HIGH &&
            nCompressionLevel != APE_COMPRESSION_LEVEL_INSANE)
        {
            DisplayProperUsage(stderr);
            return ERROR_UNDEFINED;
        }
    }

    // set the initial tick count
    g_ProcessingDuration.Reset();

    // process
#ifdef APE_SUPPORT_COMPRESS
    if (nMode == COMPRESS_MODE)
    {
        str_utfn cCompressionLevel[16]; APE_CLEAR(cCompressionLevel);
        GetAPECompressionLevelName(nCompressionLevel, cCompressionLevel, 16, false);

        fwprintf(stderr, L"Compressing (%ls)...\n", cCompressionLevel);

        // build the output filename
        HandleAuto(spInputFilename, spOutputFilename, true);

        // compress
        nRetVal = CompressFileW(spInputFilename, spOutputFilename, nCompressionLevel, &s_CallbackInfo, nThreads, APE_NULL, bReadFullInput);

        // tag
        if ((nRetVal == ERROR_SUCCESS) && (spTagSwitch != APE_NULL))
        {
            fwprintf(stderr, L"\nTagging...\n");

            nRetVal = Tag(spOutputFilename, spTagSwitch);
        }
    }
    else
#endif
    if (nMode == DECOMPRESS_MODE)
    {
        fwprintf(stderr, L"Decompressing...\n");

        // build the output filename
        HandleAuto(spInputFilename, spOutputFilename, false);

        // decompress
        nRetVal = DecompressFileW(spInputFilename, spOutputFilename, &s_CallbackInfo, nThreads);
    }
    else if (nMode == VERIFY_MODE)
    {
        fwprintf(stderr, L"Verifying...\n");
        nRetVal = VerifyFileW(spInputFilename, &s_CallbackInfo, true, nThreads);
    }
    else if (nMode == VERIFY_FULL_MODE)
    {
        fwprintf(stderr, L"Full verifying...\n");
        nRetVal = VerifyFileW(spInputFilename, &s_CallbackInfo, false, nThreads);
    }
    else if (nMode == CONVERT_MODE)
    {
        fwprintf(stderr, L"Converting...\n");
        nRetVal = ConvertFileW(spInputFilename, spOutputFilename, nCompressionLevel, &s_CallbackInfo, nThreads);
    }
    else if (nMode == TAG_MODE)
    {
        fwprintf(stderr, L"Tagging...\n");
        nRetVal = Tag(spInputFilename, spTagSwitch);
    }
    else if (nMode == REMOVE_TAG_MODE)
    {
        fwprintf(stderr, L"Removing tags...\n");
        nRetVal = RemoveTags(spInputFilename);
    }
    else if (nMode == CONVERT_TAG_TO_LEGACY_MODE)
    {
        fwprintf(stderr, L"Converting tag to ID3v1...\n");
        nRetVal = ConvertTagsToLegacy(spInputFilename);
    }
    else if (nMode == TAG_DUMP_MODE)
    {
        fwprintf(stderr, L"Dumping tags...\n");
        nRetVal = TagDump(spInputFilename);
    }

    if (nRetVal == ERROR_SUCCESS)
        fwprintf(stderr, L"\nSuccess\n");
    else
        fwprintf(stderr, L"\nError: %i\n", nRetVal);

    return nRetVal;
}
