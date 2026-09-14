#include "stdafx.h"
#include "MAC.h"
#include "APEInfoDlg.h"
#include "MACDlg.h"

CAPEInfoDlg::CAPEInfoDlg(CMACDlg * pMACDlg, CStringArray & aryFiles) :
    CDialog(CAPEInfoDlg::IDD, pMACDlg),
    m_dlgFormat(pMACDlg, this)
{
    m_pMACDlg = pMACDlg;
    m_aryFiles.Copy(aryFiles);
}

void CAPEInfoDlg::DoDataExchange(CDataExchange * pDX)
{
    CDialog::DoDataExchange(pDX);

    DDX_Control(pDX, IDC_FILE_LIST, m_ctrlFiles);
    DDX_Control(pDX, IDC_TABS, m_ctrlTabs);
    DDX_Control(pDX, IDC_FILES_SELECT_ALL, m_ctrlSelectAll);
    DDX_Control(pDX, IDC_FILES_SELECT_NONE, m_ctrlSelectNone);
    DDX_Control(pDX, IDOK, m_ctrlOK);
}

BEGIN_MESSAGE_MAP(CAPEInfoDlg, CDialog)
    ON_BN_CLICKED(IDC_FILES_SELECT_ALL, &CAPEInfoDlg::OnBnClickedFilesSelectAll)
    ON_BN_CLICKED(IDC_FILES_SELECT_NONE, &CAPEInfoDlg::OnBnClickedFilesSelectNone)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_FILE_LIST, &CAPEInfoDlg::OnLvnItemchangedFileList)
END_MESSAGE_MAP()

BOOL CAPEInfoDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // set the font to all the controls
    SetFont(theApp.GetFont());
    SendMessageToDescendants(WM_SETFONT, reinterpret_cast<WPARAM>(theApp.GetFont()->GetSafeHandle()), MAKELPARAM(false, 0), true);

    CRect rectFiles; m_ctrlFiles.GetWindowRect(&rectFiles); ScreenToClient(&rectFiles);
    m_ctrlFiles.InsertColumn(0, _T("Files"), LVCFMT_LEFT, rectFiles.Width() - GetSystemMetrics(SM_CXVSCROLL) - 2);
    for (int z = 0; z < m_aryFiles.GetSize(); z++)
    {
        CFilename fnFile(m_aryFiles[z]);
        m_ctrlFiles.InsertItem(z, fnFile.GetNameAndExtension(), -1);
    }

    m_ctrlTabs.InsertItem(0, _T("Format"));
    //m_ctrlTabs.InsertItem(1, _T("Other Stuff"));

    CRect rectTabs; m_ctrlTabs.GetWindowRect(&rectTabs); ScreenToClient(&rectTabs);
    CRect rectTab; m_ctrlTabs.GetItemRect(0, &rectTab);
    int nTabTitleBarHeight = rectTab.Height() + theApp.GetSize(2);

    m_dlgFormat.Create(IDD_APE_INFO_FORMAT, this);

    int nButtonHeight = theApp.GetTextExtent(this, _T("Ay")).cy + theApp.GetSize(8);
    CRect rectSelectAll; m_ctrlSelectAll.GetWindowRect(&rectSelectAll); ScreenToClient(&rectSelectAll);
    m_ctrlSelectAll.SetWindowPos(APE_NULL, rectSelectAll.left, rectSelectAll.top - (nButtonHeight - rectSelectAll.Height()), rectSelectAll.Width(), nButtonHeight, SWP_NOZORDER);
    m_ctrlSelectAll.GetWindowRect(&rectSelectAll); ScreenToClient(&rectSelectAll); // update because we use below
    CRect rectSelectNone; m_ctrlSelectNone.GetWindowRect(&rectSelectNone); ScreenToClient(&rectSelectNone);
    m_ctrlSelectNone.SetWindowPos(APE_NULL, rectSelectNone.left, rectSelectNone.top - (nButtonHeight - rectSelectNone.Height()), rectSelectNone.Width(), nButtonHeight, SWP_NOZORDER);
    CRect rectOK; m_ctrlOK.GetWindowRect(&rectOK); ScreenToClient(&rectOK);
    m_ctrlOK.SetWindowPos(APE_NULL, rectOK.left, rectOK.top - (nButtonHeight - rectOK.Height()), rectOK.Width(), nButtonHeight, SWP_NOZORDER);

    m_ctrlFiles.SetWindowPos(APE_NULL, rectFiles.left, rectFiles.top, rectFiles.Width(), rectSelectAll.top - rectFiles.top - theApp.GetSize(8), SWP_NOZORDER);

    m_ctrlTabs.SetWindowPos(APE_NULL, rectTabs.left, rectTabs.top, rectTabs.Width(), rectSelectAll.top - rectTabs.top - theApp.GetSize(8), SWP_NOZORDER);
    m_ctrlTabs.GetWindowRect(&rectTabs); ScreenToClient(&rectTabs); // update

    int nBorder = theApp.GetSize(8);
    m_dlgFormat.SetWindowPos(&m_ctrlTabs, rectTabs.left + nBorder, rectTabs.top + nTabTitleBarHeight + nBorder, rectTabs.Width() - (2 * nBorder), rectTabs.Height() - (2 * nBorder) - nTabTitleBarHeight, SWP_SHOWWINDOW);
    m_dlgFormat.Layout();

    m_dlgFormat.SetFiles(m_aryFiles);

    return true;  // return TRUE unless you set the focus to a control
                  // EXCEPTION: OCX Property Pages should return FALSE
}

void CAPEInfoDlg::OnBnClickedFilesSelectAll()
{
    for (int z = 0; z < m_ctrlFiles.GetItemCount(); z++)
        m_ctrlFiles.SetItemState(z, LVIS_SELECTED, LVIS_SELECTED);
}

void CAPEInfoDlg::OnBnClickedFilesSelectNone()
{
    for (int z = 0; z < m_ctrlFiles.GetItemCount(); z++)
        m_ctrlFiles.SetItemState(z, 0, LVIS_SELECTED);
}

void CAPEInfoDlg::OnLvnItemchangedFileList(NMHDR *, LRESULT * pResult)
{
    CStringArray arySelected;
    POSITION Pos = m_ctrlFiles.GetFirstSelectedItemPosition();
    while (Pos)
    {
        int nIndex = m_ctrlFiles.GetNextSelectedItem(Pos);
        arySelected.Add(m_aryFiles[nIndex]);
    }

    m_dlgFormat.SetFiles(arySelected);

    *pResult = 0;
}
