#pragma once

class CAboutDlg : public CDialog
{
public:
    CAboutDlg(CWnd * pParent = APE_NULL);

    enum { IDD = IDD_ABOUT };

protected:
    virtual void DoDataExchange(CDataExchange * pDX) APE_OVERRIDE;
    virtual BOOL OnInitDialog() APE_OVERRIDE;

    afx_msg HBRUSH OnCtlColor(CDC * pDC, CWnd * pWnd, UINT nCtlColor);
    afx_msg void OnPaint();
    afx_msg void OnMoving(UINT, LPRECT);
    DECLARE_MESSAGE_MAP()

    CString GetCPU();

    bool m_bFontsCreated;
    CFont m_fontSmall;
    CFont m_fontLarge;
};
