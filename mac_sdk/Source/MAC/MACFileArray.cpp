#include "stdafx.h"
#include "MACFileArray.h"
#include "APETicks.h"

MAC_FILE_ARRAY::MAC_FILE_ARRAY() : CArray<MAC_FILE *, MAC_FILE *>()
{
}

MAC_FILE_ARRAY::~MAC_FILE_ARRAY()
{
    RemoveAll();
}

INT_PTR MAC_FILE_ARRAY::GetSize()
{
    return Parent::GetSize();
}

MAC_FILE * MAC_FILE_ARRAY::operator[](INT_PTR nIndex)
{
    return Parent::ElementAt(nIndex);
}

MAC_FILE * MAC_FILE_ARRAY::ElementAt(INT_PTR nIndex)
{
    return Parent::ElementAt(nIndex);
}

void MAC_FILE_ARRAY::Add(MAC_FILE * pFile)
{
    Parent::Add(pFile);
}

void MAC_FILE_ARRAY::RemoveAt(INT_PTR nIndex)
{
    delete Parent::ElementAt(nIndex);
    Parent::RemoveAt(nIndex);
}

void MAC_FILE_ARRAY::RemoveAll()
{
    for (int z = 0; z < GetSize(); z++)
        delete Parent::ElementAt(z);
    Parent::RemoveAll();
}

MAC_FILE ** MAC_FILE_ARRAY::GetData()
{
    MAC_FILE ** ppFile = Parent::GetData();
    return ppFile;
}

bool MAC_FILE_ARRAY::PrepareForProcessing(CMACProcessFiles * pProcessFiles)
{
    // prepare the files
    for (int z = 0; z < GetSize(); z++)
        ElementAt(z)->PrepareForProcessing(pProcessFiles);

    // store the start time
    m_StartProcessingDuration.Reset();

    return true;
}

double MAC_FILE_ARRAY::GetTotalInputBytes()
{
    double dTotalBytes = 0;

    for (int z = 0; z < GetSize(); z++)
    {
        dTotalBytes += ElementAt(z)->m_dInputFileBytes;
    }

    return dTotalBytes;
}

double MAC_FILE_ARRAY::GetTotalOutputBytes()
{
    double dTotalBytes = 0;

    for (int z = 0; z < GetSize(); z++)
    {
        dTotalBytes += ElementAt(z)->m_dOutputFileBytes;
    }

    return dTotalBytes;
}

bool MAC_FILE_ARRAY::GetProcessingInfo(bool bStopped, int & rnRunning, bool & rbAllDone)
{
    rnRunning = 0;
    rbAllDone = true;

    for (int z = 0; z < GetSize(); z++)
    {
        MAC_FILE * pInfo = ElementAt(z);

        // running
        if (pInfo->m_bStarted && (pInfo->m_bDone == false))
            rnRunning++;

        // all done (if never started and we've stopped, don't reset rbAllDone to false)
        if (pInfo->m_bStarted || (bStopped == false))
        {
            if (pInfo->m_bDone == false)
                rbAllDone = false;
        }
    }

    return true;
}

bool MAC_FILE_ARRAY::GetProcessingProgress(double & rdProgress, double & rdSecondsLeft, int nPausedTotalMS)
{
    double dTotal = 0;
    double dDone = 0;

    for (int z = 0; z < GetSize(); z++)
    {
        MAC_FILE * pInfo = ElementAt(z);
        dTotal += pInfo->m_dInputFileBytes;

        if (pInfo->m_bDone == false)
        {
            if (pInfo->m_bStarted)
            {
                double dProgress = pInfo->GetProgress();
                dDone += pInfo->m_dInputFileBytes * dProgress;
            }
        }
        else
        {
            dDone += pInfo->m_dInputFileBytes;
        }
    }

    double dElapsed = static_cast<double>(m_StartProcessingDuration.GetElapsedMS()) - static_cast<double>(nPausedTotalMS);

    rdProgress = dDone / dTotal;
    if (rdProgress <= 0)
        rdSecondsLeft = FP_INFINITE;
    else
        rdSecondsLeft = ((dElapsed / rdProgress) - dElapsed) / 1000;

    return true;
}

int MAC_FILE_ARRAY::GetFileIndex(const CString & strFilename)
{
    for (int z = 0; z < GetSize(); z++)
    {
        MAC_FILE * pInfo = ElementAt(z);
        if (pInfo->m_strInputFilename.CompareNoCase(strFilename) == 0)
            return z;
    }

    return -1;
}

bool MAC_FILE_ARRAY::GetContainsFile(const CString & strFilename)
{
    return (GetFileIndex(strFilename) >= 0);
}
