#pragma once
#include "afxwin.h"
#include "afxcmn.h"
#include "APETag.h"

class CMACDlg;

class CAPEInfoFormatDlg : public CDialog
{
    DECLARE_DYNAMIC(CAPEInfoFormatDlg)

public:
    CAPEInfoFormatDlg(CMACDlg * pMACDlg, CWnd * pParent = APE_NULL);
    virtual ~CAPEInfoFormatDlg() APE_OVERRIDE;

    void Layout();
    bool SetFiles(CStringArray & aryFiles);

    enum { IDD = IDD_APE_INFO_FORMAT };

protected:
    virtual void DoDataExchange(CDataExchange * pDX) APE_OVERRIDE;
    virtual BOOL OnInitDialog() APE_OVERRIDE;
    DECLARE_MESSAGE_MAP()

    CMACDlg * m_pMACDlg;
    CString GetSummary(const CString & strFilename);
    CStringArray m_aryFiles;

public:
    CRichEditCtrl m_ctrlFormat;
};
