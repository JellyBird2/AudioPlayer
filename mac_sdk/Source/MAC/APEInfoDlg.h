#pragma once
#include "afxcmn.h"
#include "APEInfoFormatDlg.h"
class CMACDlg;

class CAPEInfoDlg : public CDialog
{
public:
    CAPEInfoDlg(CMACDlg * pMACDlg, CStringArray & aryFiles);

    enum { IDD = IDD_APE_INFO };

protected:
    virtual void DoDataExchange(CDataExchange * pDX) APE_OVERRIDE;
    virtual BOOL OnInitDialog() APE_OVERRIDE;

    DECLARE_MESSAGE_MAP()

    CMACDlg * m_pMACDlg;
    CStringArray m_aryFiles;
    CAPEInfoFormatDlg m_dlgFormat;

public:
    afx_msg void OnBnClickedFilesSelectAll();
    afx_msg void OnBnClickedFilesSelectNone();
    afx_msg void OnLvnItemchangedFileList(NMHDR * pNMHDR, LRESULT * pResult);

    CListCtrl m_ctrlFiles;
    CTabCtrl m_ctrlTabs;
    CButton m_ctrlSelectAll;
    CButton m_ctrlSelectNone;
    CButton m_ctrlOK;
};
