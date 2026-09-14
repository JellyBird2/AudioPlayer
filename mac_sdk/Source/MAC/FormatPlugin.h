#pragma once

#include "Format.h"
#include "Markup.h"
class CMACDlg;

class CFormatPluginLevelInfo
{
public:
    CFormatPluginLevelInfo()
    {
    }

    CFormatPluginLevelInfo(CMarkup & XML)
    {
        m_strName = XML.GetTagName();
        m_aryInputExtensions.InitFromList(XML.GetChildData(_T("InputExtensions")), _T(";"));
        m_strOutputExtension = XML.GetChildData(_T("OutputExtension"));
        m_strApplication = XML.GetChildData(_T("Application"));
        m_strCommandLine = XML.GetChildData(_T("CommandLine"));
        m_strSuccessReturn = XML.GetChildData(_T("SuccessReturn"));
    }

    CString m_strName;
    CStringArrayEx m_aryInputExtensions;
    CString m_strOutputExtension;
    CString m_strApplication;
    CString m_strCommandLine;
    CString m_strSuccessReturn;
};

class CFormatPlugin : public IFormat
{
public:
    CFormatPlugin(CMACDlg * pMACDlg, int nIndex);
    virtual ~CFormatPlugin() APE_OVERRIDE;

    bool Load(const CString & strAPXFilename);

    virtual bool GetValid() APE_OVERRIDE { return m_bIsValid; }

    virtual CString GetName() APE_OVERRIDE { return m_strName; }

    virtual int Process(MAC_FILE * pInfo) APE_OVERRIDE;

    virtual bool BuildMenu(CMenu * pMenu, int nBaseID) APE_OVERRIDE;
    virtual bool ProcessMenuCommand(int nCommand) APE_OVERRIDE;

    virtual CString GetInputExtensions(APE::APE_MODES Mode) APE_OVERRIDE;
    virtual CString GetOutputExtension(APE::APE_MODES Mode, const CString & strInputFilename, int nLevel) APE_OVERRIDE;

protected:
    // helpers
    void ParseModeInfo(CMarkup & XML, APE::APE_MODES Mode, const CString & strKeyword);
    CFormatPluginLevelInfo * GetLevelInfo(APE::APE_MODES Mode, const CString & strInputFilename, int nLevel);

    // parent
    CMACDlg * m_pMACDlg;

    // properties
    bool m_bIsValid;
    int m_nIndex;

    // filename
    CString m_strAPXFilename;

    // general
    CString m_strName;
    CString m_strURL;
    CString m_strAuthor;
    CString m_strVersion;
    CString m_strDescription;

    // mode info
    CArray<CFormatPluginLevelInfo, CFormatPluginLevelInfo &> m_aryModeInfo[APE::MODE_COUNT];

    // configuration
    bool m_bHasConfiguration;
    CString m_strConfigureDescription[3];
    CString m_strConfigureValue[3];
};
