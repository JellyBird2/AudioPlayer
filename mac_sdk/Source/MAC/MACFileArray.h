#pragma once

#include "MACFile.h"

class MAC_FILE_ARRAY : private CArray<MAC_FILE *, MAC_FILE *>
{
public:
    typedef CArray<MAC_FILE *, MAC_FILE *> Parent;

    // construction / destruction
    MAC_FILE_ARRAY();
    virtual ~MAC_FILE_ARRAY() APE_OVERRIDE;

    // operations
    INT_PTR GetSize();
    MAC_FILE * operator[](INT_PTR nIndex);
    MAC_FILE * ElementAt(INT_PTR nIndex);
    void Add(MAC_FILE * pFile);
    void RemoveAt(INT_PTR nIndex);
    void RemoveAll();
    MAC_FILE ** GetData();

    bool PrepareForProcessing(CMACProcessFiles * pProcessFiles);

    double GetTotalInputBytes();
    double GetTotalOutputBytes();
    bool GetProcessingProgress(double & rdProgress, double & rdSecondsLeft, int nPausedTotalMS);
    bool GetProcessingInfo(bool bStopped, int & rnRunning, bool & rbAllDone);

    int GetFileIndex(const CString & strFilename);
    bool GetContainsFile(const CString & strFilename);

    // data
    CAPEDuration m_StartProcessingDuration;
};
