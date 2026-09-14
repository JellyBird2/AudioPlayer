#include "stdafx.h"
#include "MAC.h"
#include "OptionsDlg.h"

#include "OptionsProcessingDlg.h"
#include "OptionsOutputDlg.h"
#include "APEButtons.h"
#include "MACDlg.h"
#include "OptionsShared.h"

COptionsDlg::COptionsDlg(CMACDlg * pMACDlg, CWnd * pParent)
    : CDialog(COptionsDlg::IDD, pParent)
{
    m_pMACDlg = pMACDlg;
}

void COptionsDlg::DoDataExchange(CDataExchange * pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PAGE_LIST, m_ctrlPageList);
    DDX_Control(pDX, IDC_PAGE_FRAME, m_ctrlPageFrame);
}

BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_PAGE_LIST, &COptionsDlg::OnItemchangedPageList)
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_MOVING()
END_MESSAGE_MAP()

BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // set the font to all the controls
    SetFont(theApp.GetFont());
    SendMessageToDescendants(WM_SETFONT, reinterpret_cast<WPARAM>(theApp.GetFont()->GetSafeHandle()), MAKELPARAM(false, 0), true);

    m_ctrlPageList.SetImageList(theApp.GetImageList(CMACApp::Image_OptionsList), LVSIL_SMALL);

    m_aryPages.Add(new OPTIONS_PAGE_PROCESSING(m_pMACDlg, this));
    m_aryPages.Add(new OPTIONS_PAGE_OUTPUT(m_pMACDlg, this));

    for (int z = 0; z < m_aryPages.GetSize(); z++)
        m_ctrlPageList.InsertItem(z, m_aryPages[z]->m_strCaption, m_aryPages[z]->m_nIcon);

    m_ctrlPageList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

    UpdatePage();

    return true;  // return TRUE unless you set the focus to a control
                  // EXCEPTION: OCX Property Pages should return FALSE
}

void COptionsDlg::OnItemchangedPageList(NMHDR *, LRESULT * pResult)
{
    // get page
    int nPage = GetSelectedPage();
    if (nPage < 0)
    {
        // if no page is selected (off the bottom) just do nothing
    }
    else
    {
        UpdatePage();
    }

    *pResult = 0;
}

bool COptionsDlg::UpdatePage()
{
    // hide all the pages
    for (int z = 0; z < m_aryPages.GetSize(); z++)
        m_aryPages[z]->m_spDialog->ShowWindow(SW_HIDE);
    m_ctrlPageFrame.SetWindowText(_T(""));

    // get the new page
    int nPage = GetSelectedPage();

    // update
    if ((nPage >= 0) && (nPage < m_aryPages.GetSize()))
    {
        // frame text
        m_ctrlPageFrame.SetWindowText(m_aryPages[nPage]->m_strCaption);

        // position / visibility
        const int nBorderWidth = theApp.GetSize(4);
        const int nTopBorder = m_pMACDlg->GetFontHeight() + theApp.GetSize(1);
        CRect rectFrame; m_ctrlPageFrame.GetWindowRect(&rectFrame); ScreenToClient(&rectFrame);

        m_aryPages[nPage]->m_spDialog->SetWindowPos(APE_NULL, rectFrame.left + nBorderWidth, rectFrame.top + nBorderWidth + nTopBorder,
            rectFrame.Width() - (2 * nBorderWidth), rectFrame.Height() - (2 * nBorderWidth) - nTopBorder, SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    CRect rectWindow;
    GetWindowRect(&rectWindow);
    int nHeight = 0;
    nHeight = APE_MAX(nHeight, m_aryPages[0]->m_nIdealHeight);
    nHeight = APE_MAX(nHeight, m_aryPages[1]->m_nIdealHeight);
    nHeight += theApp.GetSize(50) + m_pMACDlg->GetFontHeight() + theApp.GetSize(10);
    int nWidth = theApp.GetSize(900);
    SetWindowPos(APE_NULL, 0, 0, nWidth, nHeight, SWP_NOMOVE);

    return true;
}

int COptionsDlg::GetSelectedPage() const
{
    int nPage = -1;
    POSITION Pos = m_ctrlPageList.GetFirstSelectedItemPosition();
    if (Pos != APE_NULL)
        nPage = m_ctrlPageList.GetNextSelectedItem(Pos);
    return nPage;
}

void COptionsDlg::OnDestroy()
{
    while (m_aryPages.GetSize() > 0)
    {
        APE_SAFE_DELETE(m_aryPages[m_aryPages.GetUpperBound()])
        m_aryPages.RemoveAt(m_aryPages.GetUpperBound());
    }

    CDialog::OnDestroy();
}

void COptionsDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);

    if (GetDlgItem(IDOK) == APE_NULL)
        return;

    // sizes
    int nBorder = theApp.GetSize(8);
    int nPageTopBorder = theApp.GetSize(4);
    int nTopBorder = theApp.GetSize(10);
    int nButtonHeight = m_pMACDlg->GetControlHeight(GetDlgItem(IDOK));

    // get list width
    IMAGEINFO Info; APE_CLEAR(Info);
    CImageList * pImageList = m_ctrlPageList.GetImageList(LVSIL_SMALL);
    if (pImageList != APE_NULL)
        pImageList->GetImageInfo(0, &Info);
    int nListWidth = theApp.GetTextExtent(this, _T("Processing")).cx + theApp.GetSize(20) + (Info.rcImage.right - Info.rcImage.left);

    // buttons
    GetDlgItem(IDOK)->SetWindowPos(APE_NULL, nBorder, cy - (nButtonHeight * 2) - (nBorder * 2), nListWidth, nButtonHeight, SWP_NOZORDER);
    GetDlgItem(IDCANCEL)->SetWindowPos(APE_NULL, nBorder, cy - nButtonHeight - (nBorder * 1), nListWidth, nButtonHeight, SWP_NOZORDER);

    // list
    int nTopOK = cy - (nButtonHeight * 2);
    int nPageListHeight = nTopOK - nTopBorder - (nBorder * 3);
    m_ctrlPageList.SetWindowPos(APE_NULL, nBorder, nTopBorder, nListWidth, nPageListHeight, SWP_NOZORDER);

    // frame
    CRect rectPageList; m_ctrlPageList.GetClientRect(&rectPageList);
    m_ctrlPageFrame.SetWindowPos(APE_NULL, rectPageList.right + (nBorder * 3), nPageTopBorder, cx - rectPageList.right - (nBorder * 4), cy - (1 * nBorder) - nPageTopBorder, SWP_NOZORDER);
}

void COptionsDlg::OnMoving(UINT, LPRECT pRect)
{
    CapMoveToMonitor(GetSafeHwnd(), pRect);
}

void COptionsDlg::OnOK()
{
    for (int z = 0; z < m_aryPages.GetSize(); z++)
        m_aryPages[z]->m_spDialog->SendMessage(UM_SAVE_PAGE_OPTIONS);

    theApp.GetSettings()->Save();

    CDialog::OnOK();
}
