#include "stdafx.h"
#include "MAC.h"
#include "OptionsProcessingDlg.h"
#include "OptionsShared.h"
#include "APEButtons.h"
#include "MACDlg.h"

COptionsProcessingDlg::COptionsProcessingDlg(CMACDlg * pMACDlg, OPTIONS_PAGE * pPage, CWnd * pParent)
    : CDialog(COptionsProcessingDlg::IDD, pParent)
{
    m_pMACDlg = pMACDlg;
    m_pPage = pPage;
    m_bCompletionSound = false;
    m_nPriorityMode = -1;
    m_nSimultaneousFiles = -1;
    m_bStopOnError = false;
    m_bAutoVerify = false;
    m_nVerifyMode = -1;
}

void COptionsProcessingDlg::DoDataExchange(CDataExchange * pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_OTHER_PICTURE, m_ctrlOtherPicture);
    DDX_Control(pDX, IDC_ERROR_BEHAVIOR_PICTURE, m_ctrlErrorBehaviorPicture);
    DDX_Control(pDX, IDC_VERIFY_PICTURE, m_ctrlVerifyPicture);
    DDX_Control(pDX, IDC_GENERAL_PICTURE, m_ctrlGeneralPicture);
    DDX_Control(pDX, IDC_SIZE_PICTURE, m_ctrlSizePicture);
    DDX_Check(pDX, IDC_COMPLETION_SOUND_CHECK, m_bCompletionSound);
    DDX_CBIndex(pDX, IDC_PRIORITY_COMBO, m_nPriorityMode);
    DDX_CBIndex(pDX, IDC_SIMULTANEOUS_FILES_COMBO, m_nSimultaneousFiles);
    DDX_Check(pDX, IDC_STOP_ON_ERROR_CHECK, m_bStopOnError);
    DDX_Check(pDX, IDC_AUTO_VERIFY_CHECK, m_bAutoVerify);
    DDX_CBIndex(pDX, IDC_VERIFY_MODE_COMBO, m_nVerifyMode);
    DDX_Control(pDX, IDC_PROGRAM_SIZE_COMBO, m_ctrlProgramSize);
    DDX_Control(pDX, IDC_FONT_SIZE_COMBO, m_ctrlFontSize);
}

BEGIN_MESSAGE_MAP(COptionsProcessingDlg, CDialog)
    ON_WM_SIZE()
    ON_BN_CLICKED(IDOK, &COptionsProcessingDlg::OnOK)
    ON_BN_CLICKED(IDCANCEL, &COptionsProcessingDlg::OnCancel)
    ON_WM_DESTROY()
    ON_MESSAGE(UM_SAVE_PAGE_OPTIONS, &COptionsProcessingDlg::OnSaveOptions)
END_MESSAGE_MAP()

BOOL COptionsProcessingDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // set the font to all the controls
    SetFont(theApp.GetFont());
    SendMessageToDescendants(WM_SETFONT, reinterpret_cast<WPARAM>(theApp.GetFont()->GetSafeHandle()), MAKELPARAM(false, 0), true);

    // images
    HICON hIcon = theApp.GetImageList(CMACApp::Image_OptionsPages)->ExtractIcon(TBB_OPTIONS_PROCESSING_GENERAL);
    m_ctrlGeneralPicture.SetIcon(hIcon);
    m_aryIcons.Add(hIcon);

    hIcon = theApp.GetImageList(CMACApp::Image_OptionsPages)->ExtractIcon(TBB_OPTIONS_PROCESSING_VERIFY);
    m_ctrlVerifyPicture.SetIcon(hIcon);
    m_aryIcons.Add(hIcon);

    hIcon = theApp.GetImageList(CMACApp::Image_OptionsPages)->ExtractIcon(TBB_OPTIONS_PAGE_SIZE);
    m_ctrlSizePicture.SetIcon(hIcon);
    m_aryIcons.Add(hIcon);

    hIcon = theApp.GetImageList(CMACApp::Image_OptionsPages)->ExtractIcon(TBB_OPTIONS_PROCESSING_ERROR_BEHAVIOR);
    m_ctrlErrorBehaviorPicture.SetIcon(hIcon);
    m_aryIcons.Add(hIcon);

    hIcon = theApp.GetImageList(CMACApp::Image_OptionsPages)->ExtractIcon(TBB_OPTIONS_PROCESSING_OTHER);
    m_ctrlOtherPicture.SetIcon(hIcon);
    m_aryIcons.Add(hIcon);

    // fill size lists
    int arySizes[13] = { 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 175, 200 };
    for (int z = 0; z < static_cast<int>(_countof(arySizes)); z++)
    {
        CString strSize;
        strSize.Format(_T("%d%%"), arySizes[z]);
        m_ctrlProgramSize.AddString(strSize);
        m_ctrlFontSize.AddString(strSize);
    }

    CString strSize;
    strSize.Format(_T("%d%%"), theApp.GetSettings()->LoadSetting(_T("Size"), 100));
    m_ctrlProgramSize.SelectString(0, strSize);
    strSize.Format(_T("%d%%"), theApp.GetSettings()->LoadSetting(_T("Font Size"), 100));
    m_ctrlFontSize.SelectString(0, strSize);

    // settings
    m_nSimultaneousFiles = theApp.GetSettings()->m_nProcessingSimultaneousFiles - 1;
    m_nPriorityMode = theApp.GetSettings()->m_nProcessingPriorityMode;
    m_bStopOnError = theApp.GetSettings()->m_bProcessingStopOnErrors;
    m_bCompletionSound = theApp.GetSettings()->m_bProcessingPlayCompletionSound;
    m_bAutoVerify = theApp.GetSettings()->m_bProcessingAutoVerifyOnCreation;
    m_nVerifyMode = theApp.GetSettings()->m_nProcessingVerifyMode;

    // layout (to get the ideal height)
    Layout();

    // update
    UpdateData(false);

    return true;  // return TRUE unless you set the focus to a control
                  // EXCEPTION: OCX Property Pages should return FALSE
}

void COptionsProcessingDlg::OnDestroy()
{
    CDialog::OnDestroy();

    for (int z = 0; z < m_aryIcons.GetSize(); z++)
        DestroyIcon(m_aryIcons[z]);
    m_aryIcons.RemoveAll();
}

LRESULT COptionsProcessingDlg::OnSaveOptions(WPARAM, LPARAM)
{
    UpdateData(true);

    // settings
    theApp.GetSettings()->m_nProcessingSimultaneousFiles = m_nSimultaneousFiles + 1;
    theApp.GetSettings()->m_nProcessingPriorityMode = m_nPriorityMode;
    theApp.GetSettings()->m_bProcessingStopOnErrors = m_bStopOnError;
    theApp.GetSettings()->m_bProcessingPlayCompletionSound = m_bCompletionSound;
    theApp.GetSettings()->m_bProcessingAutoVerifyOnCreation = m_bAutoVerify;
    theApp.GetSettings()->m_nProcessingVerifyMode = m_nVerifyMode;

    // sizes
    CString strSize;
    m_ctrlProgramSize.GetWindowText(strSize);
    theApp.GetSettings()->SaveSetting(_T("Size"), _ttoi(strSize));

    m_ctrlFontSize.GetWindowText(strSize);
    theApp.GetSettings()->SaveSetting(_T("Font Size"), _ttoi(strSize));

    return true;
}

void COptionsProcessingDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);
    Layout();
}

void COptionsProcessingDlg::OnOK()
{

}

void COptionsProcessingDlg::OnCancel()
{

}

void COptionsProcessingDlg::Layout()
{
    // layout
    CRect rectLayout; GetClientRect(&rectLayout);
    int nBorder = theApp.GetSize(8);
    rectLayout.DeflateRect(nBorder, nBorder, nBorder, nBorder);
    int nLeft = rectLayout.left;

    m_pMACDlg->LayoutControlTopWithDivider(GetDlgItem(IDC_TITLE1), GetDlgItem(IDC_DIVIDER1), GetDlgItem(IDC_GENERAL_PICTURE), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_STATIC1), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_SIMULTANEOUS_FILES_COMBO), rectLayout, false, true);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_STATIC2), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_PRIORITY_COMBO), rectLayout, false, true);

    rectLayout.left = nLeft;
    m_pMACDlg->LayoutControlTopWithDivider(GetDlgItem(IDC_TITLE2), GetDlgItem(IDC_DIVIDER2), GetDlgItem(IDC_VERIFY_PICTURE), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_STATIC3), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_VERIFY_MODE_COMBO), rectLayout, false, true);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_AUTO_VERIFY_CHECK), rectLayout);

    rectLayout.left = nLeft;
    m_pMACDlg->LayoutControlTopWithDivider(GetDlgItem(IDC_TITLE5), GetDlgItem(IDC_DIVIDER5), GetDlgItem(IDC_SIZE_PICTURE), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_STATIC4), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_PROGRAM_SIZE_COMBO), rectLayout, false, true);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_STATIC5), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_FONT_SIZE_COMBO), rectLayout, false, true);

    rectLayout.left = nLeft;
    m_pMACDlg->LayoutControlTopWithDivider(GetDlgItem(IDC_TITLE3), GetDlgItem(IDC_DIVIDER3), GetDlgItem(IDC_ERROR_BEHAVIOR_PICTURE), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_STOP_ON_ERROR_CHECK), rectLayout);
    rectLayout.top += nBorder; // extra space

    rectLayout.left = nLeft;
    m_pMACDlg->LayoutControlTopWithDivider(GetDlgItem(IDC_TITLE4), GetDlgItem(IDC_DIVIDER4), GetDlgItem(IDC_OTHER_PICTURE), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_COMPLETION_SOUND_CHECK), rectLayout);
    m_pMACDlg->LayoutControlTop(GetDlgItem(IDC_SHOW_EXTERNAL_WINDOWS), rectLayout);

    // update ideal height
    m_pPage->m_nIdealHeight = rectLayout.top + nBorder;
}
