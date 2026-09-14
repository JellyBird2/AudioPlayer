#include "stdafx.h"
#include "resource.h"
#include "MAC.h"
#include "MACDlg.h"
#include "AboutDlg.h"

#include "FolderDialog.h"
#include "OptionsDlg.h"
#include "FormatArray.h"
#include "APEInfoDlg.h"
#include "APEButtons.h"
#include "APETicks.h"

using namespace APE;

#define IDT_UPDATE_PROGRESS            1
#define IDT_UPDATE_PROGRESS_MS         100
#define IDT_LAYOUT_WINDOW              2
#define IDT_UPDATE_ENBALBED_STATES     3
#define IDT_SHOW_MENU                  4

#define WM_DPICHANGED              0x02E0

#define TBI(ID) (m_ctrlToolbar.GetToolBarCtrl().CommandToIndex(ID))

static LRESULT CALLBACK s_MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    return CMACDlg::s_pThis->MouseProc(nCode, wParam, lParam);
}

CMACDlg * CMACDlg::s_pThis = APE_NULL;

CMACDlg::CMACDlg(CStringArrayEx * paryFiles, CWnd * pParent) :
    CDialog(CMACDlg::IDD, pParent),
    m_ctrlStatusBar(this),
    m_paryFiles(paryFiles)
{
    m_bLastLoadMenuAndToolbarProcessing = false;
    m_bInitialized = false;
    m_hAcceleratorTable = APE_NULL;
    m_nMonitorDPI = 0;
    m_nFontHeight = 0;
    m_bMenuShortcutMode = FALSE;
    m_nShowingMenu = -1;
    m_pTrackingMenu = APE_NULL;
    m_hMouseHook = APE_NULL;
    m_nTimerMenuShow = 0;

    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMACDlg::DoDataExchange(CDataExchange * pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST, m_ctrlList);
}

BEGIN_MESSAGE_MAP(CMACDlg, CDialog)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_COMMAND(ID_MENU_FILE, &CMACDlg::OnMenuFile)
    ON_COMMAND(ID_MENU_MODE, &CMACDlg::OnMenuMode)
    ON_COMMAND(ID_MENU_TOOLS, &CMACDlg::OnMenuTools)
    ON_COMMAND(ID_MENU_HELP, &CMACDlg::OnMenuHelp)
    ON_COMMAND(ID_MENU_STOP, &CMACDlg::OnMenuStop)
    ON_COMMAND(ID_MENU_PAUSE, &CMACDlg::OnMenuPause)
    ON_COMMAND(ID_FILE_PROCESS_FILES, &CMACDlg::OnFileProcessFiles)
    ON_COMMAND(ID_FILE_ADD_FILES, &CMACDlg::OnFileAddFiles)
    ON_COMMAND(ID_FILE_ADD_FOLDER, &CMACDlg::OnFileAddFolder)
    ON_COMMAND(ID_FILE_CLEAR_FILES, &CMACDlg::OnFileClearFiles)
    ON_COMMAND(ID_FILE_SELECTALL, &CMACDlg::OnFileSelectAll)
    ON_COMMAND(ID_FILE_REMOVE_FILES, &CMACDlg::OnFileRemoveFiles)
    ON_COMMAND(ID_FILE_FILE_INFO, &CMACDlg::OnFileFileInfo)
    ON_COMMAND(ID_FILE_EXIT, &CMACDlg::OnFileExit)
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_NOTIFY(TBN_DROPDOWN, AFX_IDW_TOOLBAR, &CMACDlg::OnToolbarDropDown)
    ON_COMMAND(ID_MODE_COMPRESS, &CMACDlg::OnModeCompress)
    ON_COMMAND(ID_MODE_DECOMPRESS, &CMACDlg::OnModeDecompress)
    ON_COMMAND(ID_MODE_VERIFY, &CMACDlg::OnModeVerify)
    ON_COMMAND(ID_MODE_CONVERT, &CMACDlg::OnModeConvert)
    ON_COMMAND(ID_MODE_MAKE_APL, &CMACDlg::OnModeMakeAPL)
    ON_COMMAND(ID_STOP, &CMACDlg::OnStop)
    ON_COMMAND(ID_COMPRESSION, &CMACDlg::OnCompression)
    ON_COMMAND(ID_COMPRESSION_APE_EXTRA_HIGH, &CMACDlg::OnCompressionAPEExtraHigh)
    ON_COMMAND(ID_COMPRESSION_APE_FAST, &CMACDlg::OnCompressionAPEFast)
    ON_COMMAND(ID_COMPRESSION_APE_HIGH, &CMACDlg::OnCompressionAPEHigh)
    ON_COMMAND(ID_COMPRESSION_APE_NORMAL, &CMACDlg::OnCompressionAPENormal)
    ON_COMMAND(ID_COMPRESSION_APE_INSANE, &CMACDlg::OnCompressionAPEInsane)
    ON_COMMAND_RANGE(ID_SET_COMPRESSION_FIRST, ID_SET_COMPRESSION_LAST, &CMACDlg::OnSetCompressionMenu)
    ON_WM_DESTROY()
    ON_COMMAND(ID_PAUSE, &CMACDlg::OnPause)
    ON_COMMAND(ID_STOP_AFTER_CURRENT_FILE, &CMACDlg::OnStopAfterCurrentFile)
    ON_COMMAND(ID_STOP_IMMEDIATELY, &CMACDlg::OnStopImmediately)
    ON_COMMAND(ID_HELP_HELP, &CMACDlg::OnHelpHelp)
    ON_COMMAND(ID_HELP_ABOUT, &CMACDlg::OnHelpAbout)
    ON_COMMAND(ID_HELP_WEBSITE_MONKEYS_AUDIO, &CMACDlg::OnHelpWebsiteMonkeysAudio)
    ON_COMMAND(ID_HELP_WEBSITE_MEDIA_JUKEBOX, &CMACDlg::OnHelpWebsiteJRiver)
    ON_COMMAND(ID_HELP_WEBSITE_WINAMP, &CMACDlg::OnHelpWebsiteWinamp)
    ON_COMMAND(ID_HELP_WEBSITE_EAC, &CMACDlg::OnHelpWebsiteEac)
    ON_COMMAND(ID_HELP_LICENSE, &CMACDlg::OnHelpLicense)
    ON_COMMAND(ID_TOOLS_OPTIONS, &CMACDlg::OnToolsOptions)
    ON_WM_INITMENU()
    ON_WM_INITMENUPOPUP()
    ON_WM_GETMINMAXINFO()
    ON_WM_ENDSESSION()
    ON_WM_QUERYENDSESSION()
    ON_MESSAGE(WM_DPICHANGED, &CMACDlg::OnDPIChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST, &CMACDlg::OnListItemChanged)
    ON_WM_MEASUREITEM()
    ON_WM_DRAWITEM()
    ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CMACDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // layout the window (before getting the scale so the window is on the right monitor but hide the window)
    WINDOWPLACEMENT WindowPlacement; APE_CLEAR(WindowPlacement);
    if (theApp.GetSettings()->LoadSetting(_T("Main Window Placement"), &WindowPlacement, sizeof(WindowPlacement)))
    {
        WindowPlacement.showCmd = SW_HIDE;
        SetWindowPlacement(&WindowPlacement);
    }
    else
    {
        SetWindowPos(APE_NULL, 0, 0, theApp.GetSize(780), theApp.GetSize(420), SWP_NOZORDER | SWP_HIDEWINDOW);
        CenterWindow();
    }

    // accelerator
    m_hAcceleratorTable = LoadAccelerators(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME));

    // icons
    SetIcon(m_hIcon, true);
    SetIcon(m_hIcon, false);

    // file list
    m_ctrlList.Initialize(this, &m_aryFiles);
    INITIALIZE_COMMON_CONTROL(m_ctrlList.GetSafeHwnd())

    // load scale
    LoadScale();

    // load the formats
    theApp.GetFormatArray()->Load();

    // window properties
    SetWindowText(APE_NAME);
    //SetWindowText(_T("Monkey's Audio")); // useful for screenshots

    // set the mode to the list
    m_ctrlList.SetMode(theApp.GetSettings()->GetMode());

    // load file list
    m_ctrlList.LoadFileList(GetUserDataPath() + _T("File Lists\\Current.m3u"), m_paryFiles);

    // load the mouse hook
    s_pThis = this;
    m_hMouseHook = ::SetWindowsHookEx(WH_MOUSE, s_MouseProc, APE_NULL, ::GetCurrentThreadId());

    // layout the window (again)
    APE_CLEAR(WindowPlacement);
    if (theApp.GetSettings()->LoadSetting(_T("Main Window Placement"), &WindowPlacement, sizeof(WindowPlacement)))
    {
        SetWindowPlacement(&WindowPlacement);
    }
    else
    {
        SetWindowPos(APE_NULL, 0, 0, theApp.GetSize(780), theApp.GetSize(420), SWP_NOZORDER);
        CenterWindow();
    }

    m_bInitialized = true;

    // layout window now (instead of on a timer since we just loaded)
    KillTimer(IDT_LAYOUT_WINDOW);
    LayoutWindow();

    return true;  // return TRUE  unless you set the focus to a control
}

bool CMACDlg::LoadMenuAndToolbar(bool bProcessing, bool bForce)
{
    if (m_bInitialized && (bProcessing == m_bLastLoadMenuAndToolbarProcessing) && (bForce == false))
        return true;
    m_bLastLoadMenuAndToolbarProcessing = bProcessing;
    // BTNS_WHOLEDROPDOWN -- requires IE 5.0 //

    m_menuMain.DestroyMenu();
    m_menuMain.LoadMenu(bProcessing ? IDR_STOP_MENU : IDR_MAIN_MENU);

    ChangeMenuToOwnerDraw(&m_menuMain, 0);
    UpdateEnabledStates();

    while (m_ctrlToolbar.GetToolBarCtrl().DeleteButton(0)) { }

    if (bProcessing)
    {
        AddToolbarButton(ID_STOP, TBB_STOP, _T("Stop"), TBSTYLE_DROPDOWN);
        AddToolbarButton(ID_PAUSE, TBB_PAUSE, _T("Pause"), TBSTYLE_CHECK);
    }
    else
    {
        AddToolbarButton(ID_FILE_PROCESS_FILES, TBB_COMPRESS, _T("Process"), TBSTYLE_DROPDOWN);
        AddToolbarButton(ID_COMPRESSION, TBB_COMPRESSION_EXTERNAL, _T("Mode"), TBSTYLE_DROPDOWN);
        AddToolbarButton(ID_SEPARATOR, TBB_NONE, _T(""), TBSTYLE_SEP);
        AddToolbarButton(ID_FILE_ADD_FILES, TBB_ADD_FILES, _T("Add Files"), TBSTYLE_DROPDOWN);
        AddToolbarButton(ID_FILE_ADD_FOLDER, TBB_ADD_FOLDER, _T("Add Folder"), TBSTYLE_DROPDOWN);
        AddToolbarButton(ID_SEPARATOR, TBB_NONE, _T(""), TBSTYLE_SEP);
        AddToolbarButton(ID_FILE_REMOVE_FILES, TBB_REMOVE_FILES, _T("Remove Files"), TBSTYLE_BUTTON);
        AddToolbarButton(ID_FILE_CLEAR_FILES, TBB_CLEAR_FILES, _T("Clear Files"), TBSTYLE_BUTTON);
        AddToolbarButton(ID_SEPARATOR, TBB_NONE, _T(""), TBSTYLE_SEP);
        AddToolbarButton(ID_FILE_FILE_INFO, TBB_FILE_INFO, _T("File Info"), TBSTYLE_BUTTON);
        AddToolbarButton(ID_SEPARATOR, TBB_NONE, _T(""), TBSTYLE_SEP);
        AddToolbarButton(ID_TOOLS_OPTIONS, TBB_OPTIONS, _T("Options"), TBSTYLE_BUTTON);
    }

    // layout to keep the menus updated
    LayoutWindow();

    return true;
}

bool CMACDlg::AddToolbarButton(int nID, int nBitmap, const CString & strText, int nStyle)
{
    TBBUTTON Button;
    APE_CLEAR(Button);

    Button.idCommand = nID;
    Button.fsStyle = static_cast<BYTE>(nStyle);
    Button.iBitmap = nBitmap;
    Button.iString = 0;
    Button.fsState = TBSTATE_ENABLED;

    int nIndex = m_ctrlToolbar.GetToolBarCtrl().GetButtonCount();

    m_ctrlToolbar.GetToolBarCtrl().InsertButton(nIndex, &Button);

    if (strText.IsEmpty() == false)
        m_ctrlToolbar.SetButtonText(nIndex, strText);

    return true;
}

void CMACDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // center icon in client rectangle
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // draw the icon
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialog::OnPaint();
    }
}

HCURSOR CMACDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CMACDlg::OnToolbarDropDown(NMHDR * pnmtb, LRESULT *)
{
    NMTOOLBAR * pNMToolbar = reinterpret_cast<NMTOOLBAR *>(pnmtb);
    CMenu menu;
    CMenu * pMenu = APE_NULL;

    // switch on button command id's.
    switch (pNMToolbar->iItem)
    {
        case ID_FILE_PROCESS_FILES:
        {
            menu.LoadMenu(IDR_MAIN_MENU);
            pMenu = menu.GetSubMenu(1);
            pMenu->RemoveMenu(static_cast<UINT>(pMenu->GetMenuItemCount() - 1), MF_BYPOSITION);
            pMenu->RemoveMenu(static_cast<UINT>(pMenu->GetMenuItemCount() - 1), MF_BYPOSITION);
            break;
        }
        case ID_COMPRESSION:
        {
            menu.LoadMenu(IDR_MAIN_MENU);
            pMenu = menu.GetSubMenu(1);
            if (pMenu)
                pMenu = pMenu->GetSubMenu(6);
            break;
        }
        case ID_STOP:
        {
            menu.LoadMenu(IDR_STOP_MENU);
            pMenu = menu.GetSubMenu(0);
            break;
        }
        case ID_FILE_ADD_FILES:
        {
            menu.CreatePopupMenu(); pMenu = &menu;
            if (theApp.GetSettings()->m_aryAddFilesMRU.GetSize() > 0)
            {
                for (int z = 0; z < theApp.GetSettings()->m_aryAddFilesMRU.GetSize(); z++)
                    menu.AppendMenu(MF_STRING, UINT_PTR(z) + 1000, theApp.GetSettings()->m_aryAddFilesMRU[z]);
            }
            else
            {
                menu.AppendMenu(MF_STRING | MF_GRAYED, 1000, _T("MRU List Empty"));
            }
            menu.AppendMenu(MF_SEPARATOR);
            menu.AppendMenu(MF_STRING, 1100, _T("Clear MRU List"));
            break;
        }
        case ID_FILE_ADD_FOLDER:
        {
            menu.CreatePopupMenu(); pMenu = &menu;
            if (theApp.GetSettings()->m_aryAddFolderMRU.GetSize() > 0)
            {
                for (int z = 0; z < theApp.GetSettings()->m_aryAddFolderMRU.GetSize(); z++)
                    menu.AppendMenu(MF_STRING, UINT_PTR(z) + 2000, theApp.GetSettings()->m_aryAddFolderMRU[z]);
            }
            else
            {
                menu.AppendMenu(MF_STRING | MF_GRAYED, 2000, _T("MRU List Empty"));
            }
            menu.AppendMenu(MF_SEPARATOR);
            menu.AppendMenu(MF_STRING, 2100, _T("Clear MRU List"));
            break;
        }
        default:
        {
            ASSERT(false);
            break;
        }
    }

    // load and display popup menu
    if (pMenu)
    {
        CRect rectItem;
        m_ctrlToolbar.SendMessage(TB_GETRECT, pNMToolbar->iItem, reinterpret_cast<LPARAM>(&rectItem));
        m_ctrlToolbar.ClientToScreen(&rectItem);

        ChangeMenuToOwnerDraw(pMenu);

        m_pTrackingMenu = pMenu;

        int nRetVal = pMenu->TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL | TPM_RETURNCMD,
            rectItem.left, rectItem.bottom, this, &rectItem);

        m_pTrackingMenu = APE_NULL;

        if (pNMToolbar->iItem == ID_FILE_ADD_FILES)
        {
            if (nRetVal >= 1000 && nRetVal < 1100)
            {
                int nIndex = nRetVal - 1000;
                m_strAddFilesBasePath = theApp.GetSettings()->m_aryAddFilesMRU[nIndex];
                OnFileAddFiles();
            }
            else if (nRetVal == 1100)
            {
                theApp.GetSettings()->m_aryAddFilesMRU.RemoveAll();
            }
        }
        else if (pNMToolbar->iItem == ID_FILE_ADD_FOLDER)
        {
            if (nRetVal >= 2000 && nRetVal < 2100)
            {
                int nIndex = nRetVal - 2000;
                m_ctrlList.AddFolder(theApp.GetSettings()->m_aryAddFolderMRU[nIndex]);
            }
            else if (nRetVal == 2100)
            {
                theApp.GetSettings()->m_aryAddFolderMRU.RemoveAll();
            }
        }
        else
        {
            PostMessage(WM_COMMAND, nRetVal, 0);
        }
    }
}

void CMACDlg::OnMenuFile()
{
    ShowMenu(0, true);
}

void CMACDlg::OnMenuMode()
{
    ShowMenu(1, true);
}

void CMACDlg::OnMenuTools()
{
    ShowMenu(2, true);
}

void CMACDlg::OnMenuHelp()
{
    ShowMenu(3, true);
}

void CMACDlg::OnMenuStop()
{
    ShowMenu(0, true);
}

void CMACDlg::OnMenuPause()
{
    OnPause();
}

void CMACDlg::OnFileProcessFiles()
{
    if (GetProcessing())
        return;

    // clear all the files
    for (int z = 0; z < m_aryFiles.GetSize(); z++)
        m_aryFiles[z]->m_bProcessing = false;
    m_ctrlList.Invalidate();

    // process
    if (m_aryFiles.GetSize() <= 0)
    {
        AfxMessageBox(_T("There are no files to process."));
    }
    else
    {
        m_spProcessFiles.Assign(new CMACProcessFiles);
        CreateMenu();
        UpdateWindow();
        m_spProcessFiles->Process(&m_aryFiles);
        m_ctrlStatusBar.StartProcessing();
        SetTimer(IDT_UPDATE_PROGRESS, IDT_UPDATE_PROGRESS_MS, APE_NULL);
    }
}

void CMACDlg::OnFileAddFiles()
{
    if (GetProcessing())
        return;

    // show the file dialog
    CFileDialog FileDialog(true, APE_NULL, APE_NULL,
        OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY, theApp.GetFormatArray()->GetOpenFilesFilter());

    // extra initialization (hacky, but safe)
    CSmartPtr<TCHAR> spFilename(new TCHAR [static_cast<size_t>(APE_MAX_PATH + 1) * 512], true); spFilename[0] = 0;
    FileDialog.m_ofn.lpstrInitialDir = m_strAddFilesBasePath;
    FileDialog.m_ofn.nMaxFile = APE_MAX_PATH * 512;
    FileDialog.m_ofn.lpstrFile = spFilename.GetPtr();

    if (FileDialog.DoModal() == IDOK)
    {
        m_ctrlList.StartFileInsertion(false);

        POSITION Pos = FileDialog.GetStartPosition();
        while (Pos != APE_NULL)
        {
            CString strFilename = FileDialog.GetNextPathName(Pos);
            m_ctrlList.AddFileInternal(strFilename);

            m_strAddFilesBasePath = GetDirectory(strFilename);
        }

        m_ctrlList.FinishFileInsertion();

        theApp.GetSettings()->m_aryAddFilesMRU.Remove(m_strAddFilesBasePath, false);
        while (theApp.GetSettings()->m_aryAddFilesMRU.GetSize() >= 4)
            theApp.GetSettings()->m_aryAddFilesMRU.RemoveAt(3);
        theApp.GetSettings()->m_aryAddFilesMRU.InsertAt(0, m_strAddFilesBasePath);
    }
}

void CMACDlg::OnFileAddFolder()
{
    if (GetProcessing())
        return;

    CSmartPtr<CFolderDialog> spFolderDialog(new CFolderDialog(APE_NULL, BIF_RETURNONLYFSDIRS | BIF_EDITBOX));
    if (spFolderDialog->DoModal() == IDOK)
    {
        m_ctrlList.AddFolder(spFolderDialog->GetPathName());
    }
}

void CMACDlg::OnFileSelectAll()
{
    m_ctrlList.SelectAll();
}

void CMACDlg::OnFileClearFiles()
{
    if (GetProcessing())
        return;

    m_ctrlList.RemoveAllFiles();
}

void CMACDlg::OnFileRemoveFiles()
{
    if (GetProcessing())
        return;

    m_ctrlList.RemoveSelectedFiles();
}

void CMACDlg::OnFileFileInfo()
{
    CStringArray aryFiles;
    m_ctrlList.GetFileList(aryFiles);

    if (aryFiles.GetSize() <= 0)
    {
        AfxMessageBox(_T("There are no files to show information for."));
    }
    else
    {
        CAPEInfoDlg dlgInfo(this, aryFiles);
        dlgInfo.DoModal();
    }
}

void CMACDlg::OnFileExit()
{
    PostMessage(WM_CLOSE);
}

void CMACDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);

    // layout window (on a timer so the window size and position have updated)
    // otherwise dragging from one DPI to another updates in a corrupted way
    // this also makes two OnSize(...) messages fire back to back only update the layout once
    SetTimer(IDT_LAYOUT_WINDOW, 1, APE_NULL);
}

void CMACDlg::LayoutWindow()
{
    if (m_bInitialized)
    {
        // get rectangle
        CRect rectClient; GetClientRect(&rectClient);
        int cx = rectClient.Width(); int cy = rectClient.Height();
        const int nBorderWidth = theApp.GetSize(5);

        // menu
        int nMenuHeight = theApp.GetTextExtent(this, _T("Ay")).cy + theApp.GetSize(8);
        int nMenuLeft = theApp.GetSize(8);
        for (int z = 0; z < m_aryMenus.GetSize(); z++)
        {
            CString strText; m_aryMenus[z]->GetWindowText(strText);
            CSize sizeText = theApp.GetTextExtent(this, strText);
            m_aryMenus[z]->SetWindowPos(APE_NULL, nMenuLeft, 0, sizeText.cx, nMenuHeight, SWP_NOZORDER);
            nMenuLeft += sizeText.cx + theApp.GetSize(24);
        }

        // size toolbar
        CSize sizeText = theApp.GetTextExtent(this, _T("High"));
        int nToolbarHeight = sizeText.cy + theApp.GetSize(46) + 10;
        m_ctrlToolbar.SetWindowPos(APE_NULL, 0, nMenuHeight, cx, nToolbarHeight, SWP_NOZORDER);

        // status bar
        int nStatusBarHeight = sizeText.cy + theApp.GetSize(7);
        m_ctrlStatusBar.SetWindowPos(APE_NULL, nBorderWidth, cy - nStatusBarHeight, cx - nBorderWidth, nStatusBarHeight, SWP_NOZORDER);
        m_ctrlStatusBar.UpdateStatusbarSizes();

        // list
        m_ctrlList.SetWindowPos(APE_NULL, nBorderWidth, nToolbarHeight + nMenuHeight, cx - (2 * nBorderWidth), cy - nStatusBarHeight - nBorderWidth - nMenuHeight - nToolbarHeight, SWP_NOZORDER);

        // update
        UpdateWindow();
    }
}

void CMACDlg::UpdateWindow()
{
    if (GetProcessing())
    {
        LoadMenuAndToolbar(true);
        m_ctrlToolbar.GetToolBarCtrl().CheckButton(ID_PAUSE, m_spProcessFiles->GetPaused());
    }
    else
    {
        LoadMenuAndToolbar(false);
        m_ctrlStatusBar.UpdateProgress(0, 0);
        m_ctrlToolbar.SetButtonText(static_cast<int>(TBI(ID_FILE_PROCESS_FILES)), theApp.GetSettings()->GetModeName());

        // compression level
        CString strCompressionName = theApp.GetSettings()->GetCompressionName();
        if (strCompressionName.IsEmpty())
        {
            m_ctrlToolbar.SetButtonText(static_cast<int>(TBI(ID_COMPRESSION)), _T("n/a"));
            m_ctrlToolbar.GetToolBarCtrl().EnableButton(ID_COMPRESSION, false);
        }
        else
        {
            m_ctrlToolbar.SetButtonText(static_cast<int>(TBI(ID_COMPRESSION)), strCompressionName);
            m_ctrlToolbar.GetToolBarCtrl().EnableButton(ID_COMPRESSION, true);
        }

        // toolbar icons
        int nModeBitmap = TBB_COMPRESS;
        switch (theApp.GetSettings()->GetMode())
        {
            case MODE_COMPRESS: nModeBitmap = TBB_COMPRESS; break;
            case MODE_DECOMPRESS: nModeBitmap = TBB_DECOMPRESS; break;
            case MODE_VERIFY: nModeBitmap = TBB_VERIFY; break;
            case MODE_CONVERT: nModeBitmap = TBB_CONVERT; break;
            case MODE_MAKE_APL: nModeBitmap = TBB_MAKE_APL; break;
            case MODE_CHECK: case MODE_COUNT: nModeBitmap = TBB_COMPRESS; break;
        }
        SetToolbarButtonBitmap(ID_FILE_PROCESS_FILES, nModeBitmap);

        int nCompressionBitmap = TBB_COMPRESSION_EXTERNAL;
        if (theApp.GetSettings()->GetFormat() == COMPRESSION_APE)
        {
            if (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_FAST)
                nCompressionBitmap = TBB_COMPRESSION_APE_FAST;
            else if (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_NORMAL)
                nCompressionBitmap = TBB_COMPRESSION_APE_NORMAL;
            else if (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_HIGH)
                nCompressionBitmap = TBB_COMPRESSION_APE_HIGH;
            else if (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_EXTRA_HIGH)
                nCompressionBitmap = TBB_COMPRESSION_APE_EXTRA_HIGH;
            else if (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_INSANE)
                nCompressionBitmap = TBB_COMPRESSION_APE_INSANE;
        }
        SetToolbarButtonBitmap(ID_COMPRESSION, nCompressionBitmap);
    }

    // redraw the list
    m_ctrlList.Invalidate();

    // redraw the toolbar (otherwise stale buttons appear when dragging from one monitor to the other)
    m_ctrlToolbar.Invalidate();
}

void CMACDlg::SetToolbarButtonBitmap(int nID, int nBitmap) const
{
    TBBUTTONINFO ButtonInfo;
    APE_CLEAR(ButtonInfo);
    ButtonInfo.cbSize = sizeof(ButtonInfo);
    ButtonInfo.dwMask = TBIF_IMAGE;
    m_ctrlToolbar.GetToolBarCtrl().GetButtonInfo(nID, &ButtonInfo);
    ButtonInfo.iImage = nBitmap;
    m_ctrlToolbar.GetToolBarCtrl().SetButtonInfo(nID, &ButtonInfo);
}

void CMACDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == IDT_UPDATE_PROGRESS)
    {
        KillTimer(IDT_UPDATE_PROGRESS);

        // take inventory of what's running
        double dProgress = 0; double dSecondsLeft = 0;
        if (m_aryFiles.GetProcessingProgress(dProgress, dSecondsLeft, m_spProcessFiles->GetPausedTotalMS()))
        {
            m_ctrlStatusBar.UpdateProgress(dProgress, dSecondsLeft);
        }

        // analyze
        int nRunning = 0; bool bAllDone = true;
        m_aryFiles.GetProcessingInfo(m_spProcessFiles->GetStopped(), nRunning, bAllDone);

        // start new ones if necessary
        if ((m_spProcessFiles->GetStopped() == false) && (m_spProcessFiles->GetPaused() == false))
        {
            for (int z = 0; (z < m_aryFiles.GetSize()) && (nRunning < theApp.GetSettings()->m_nProcessingSimultaneousFiles); z++)
            {
                MAC_FILE * pInfo = m_aryFiles[z];
                if (pInfo->GetNeverStarted())
                {
                    m_spProcessFiles->ProcessFile(z);
                    m_ctrlList.EnsureVisible(z, false);
                    nRunning++;
                }
            }
        }

        // output our progress
        bool bPaused = m_spProcessFiles->GetPaused();
        for (int z = 0; z < m_aryFiles.GetSize(); z++)
        {
            MAC_FILE * pInfo = m_aryFiles[z];
            if (pInfo && (pInfo->m_bNeedsUpdate || (pInfo->GetRunning() && (bPaused == false))))
            {
                CRect rectItem;
                m_ctrlList.GetItemRect(z, &rectItem, LVIR_BOUNDS);
                m_ctrlList.InvalidateRect(&rectItem);
                pInfo->m_bNeedsUpdate = false;
            }
        }

        // handle completion (or restart the timer if we're not done)
        if (bAllDone == false)
        {
            SetTimer(IDT_UPDATE_PROGRESS, IDT_UPDATE_PROGRESS_MS, APE_NULL);
        }
        else
        {
            // mark files as done
            for (int z = 0; z < m_aryFiles.GetSize(); z++)
            {
                if (m_aryFiles[z]->m_bDone == false)
                {
                    m_aryFiles[z]->m_bDone = true;
                    m_aryFiles[z]->m_nRetVal = ERROR_USER_STOPPED_PROCESSING;
                }
            }

            // clean up
            m_spProcessFiles.Delete();
            CreateMenu();
            UpdateWindow();
            m_ctrlStatusBar.SetLastProcessTotalMS(m_aryFiles.m_StartProcessingDuration.GetElapsedMS());
            m_ctrlStatusBar.UpdateFiles(&m_aryFiles);
            m_ctrlStatusBar.EndProcessing();

            // play sound
            if (theApp.GetSettings()->m_bProcessingPlayCompletionSound)
                PlayDefaultSound();
        }
    }
    else if (nIDEvent == IDT_LAYOUT_WINDOW)
    {
        // stop the timer
        KillTimer(nIDEvent);

        // layout window
        LayoutWindow();
    }
    else if (nIDEvent == IDT_UPDATE_ENBALBED_STATES)
    {
        // stop the timer
        KillTimer(nIDEvent);

        // update enabled states
        UpdateEnabledStates();
    }
    else if (nIDEvent == IDT_SHOW_MENU)
    {
        KillTimer(IDT_SHOW_MENU);
        ShowMenu(m_nTimerMenuShow);
    }

    CDialog::OnTimer(nIDEvent);
}

bool CMACDlg::SetMode(APE_MODES Mode)
{
    bool bRetVal = false;

    if (Mode != theApp.GetSettings()->GetMode())
    {
        // set the mode
        APE_MODES OriginalMode = theApp.GetSettings()->GetMode();
        theApp.GetSettings()->SetMode(Mode);

        // get the supported extensions
        CStringArrayEx aryExtensions;
        theApp.GetFormatArray()->GetInputExtensions(aryExtensions);

        // remove unsupported files (if any)
        CIntArray aryUnsupportedFiles;
        for (int z = 0; z < m_aryFiles.GetSize(); z++)
        {
            if (aryExtensions.Find(GetExtension(m_aryFiles[z]->m_strInputFilename)) == -1)
                aryUnsupportedFiles.Add(z);
        }
        aryUnsupportedFiles.SortDescending();

        if (aryUnsupportedFiles.GetSize() > 0)
        {
            if (MessageBox(_T("The new mode does not support all of the files in the list. Do you want to remove these unsupported files and continue?"), _T("Confirm Change Modes"), MB_YESNO) == IDYES)
            {
                for (int z = 0; z < aryUnsupportedFiles.GetSize(); z++)
                {
                    m_aryFiles.RemoveAt(aryUnsupportedFiles[z]);
                }

                m_ctrlList.Update();
            }
            else
            {
                theApp.GetSettings()->SetMode(OriginalMode);
            }
        }

        // set the mode in the list
        m_ctrlList.SetMode(Mode);

        // update if successful
        if (theApp.GetSettings()->GetMode() != OriginalMode)
        {
            UpdateWindow();
            bRetVal = true;
        }
    }

    return bRetVal;
}

bool CMACDlg::SetAPECompressionLevel(int nAPECompressionLevel)
{
    theApp.GetSettings()->SetCompression(COMPRESSION_APE, nAPECompressionLevel);
    UpdateWindow();
    return true;
}

void CMACDlg::OnCompression()
{
    NMTOOLBAR ToolbarNotification;
    APE_CLEAR(ToolbarNotification);
    ToolbarNotification.iItem = ID_COMPRESSION;

    LRESULT nRetVal = 0;
    OnToolbarDropDown(reinterpret_cast<NMHDR *>(&ToolbarNotification), &nRetVal);
}

void CMACDlg::OnModeCompress()
{
    SetMode(MODE_COMPRESS);
}

void CMACDlg::OnModeDecompress()
{
    SetMode(MODE_DECOMPRESS);
}

void CMACDlg::OnModeVerify()
{
    SetMode(MODE_VERIFY);
}

void CMACDlg::OnModeConvert()
{
    SetMode(MODE_CONVERT);
}

void CMACDlg::OnModeMakeAPL()
{
    SetMode(MODE_MAKE_APL);
}

void CMACDlg::OnStop()
{
    NMTOOLBAR ToolbarNotification;
    APE_CLEAR(ToolbarNotification);
    ToolbarNotification.iItem = ID_STOP;

    LRESULT nRetVal = 0;
    OnToolbarDropDown(reinterpret_cast<NMHDR *>(&ToolbarNotification), &nRetVal);
}

void CMACDlg::OnCompressionAPEFast()
{
    SetAPECompressionLevel(APE_COMPRESSION_LEVEL_FAST);
}

void CMACDlg::OnCompressionAPENormal()
{
    SetAPECompressionLevel(APE_COMPRESSION_LEVEL_NORMAL);
}

void CMACDlg::OnCompressionAPEHigh()
{
    SetAPECompressionLevel(APE_COMPRESSION_LEVEL_HIGH);
}

void CMACDlg::OnCompressionAPEExtraHigh()
{
    SetAPECompressionLevel(APE_COMPRESSION_LEVEL_EXTRA_HIGH);
}

void CMACDlg::OnCompressionAPEInsane()
{
    SetAPECompressionLevel(APE_COMPRESSION_LEVEL_INSANE);
}

void CMACDlg::OnSetCompressionMenu(UINT nID)
{
    theApp.GetFormatArray()->ProcessCompressionMenu(static_cast<int>(nID));
    UpdateWindow();
}

void CMACDlg::OnDestroy()
{
    // stop file processing
    if (m_spProcessFiles != APE_NULL)
        m_spProcessFiles->Stop(true);
    m_spProcessFiles.Delete();

    // save window placement
    WINDOWPLACEMENT WindowPlacement;
    APE_CLEAR(WindowPlacement);
    if (GetWindowPlacement(&WindowPlacement))
    {
        theApp.GetSettings()->SaveSetting("Main Window Placement", &WindowPlacement, sizeof(WindowPlacement));
    }

    // unload the formats
    theApp.GetFormatArray()->Unload();

    CDialog::OnDestroy();
}

void CMACDlg::OnPause()
{
    if (GetProcessing())
    {
        m_spProcessFiles->Pause(m_spProcessFiles->GetPaused() ? false : true);
        UpdateWindow();
    }
}

void CMACDlg::OnStopAfterCurrentFile()
{
    if (GetProcessing())
    {
        m_spProcessFiles->Stop(false);
        UpdateWindow();
    }
}

void CMACDlg::OnStopImmediately()
{
    if (GetProcessing())
    {
        m_spProcessFiles->Stop(true);
        UpdateWindow();
    }
}

void CMACDlg::OnHelpHelp()
{
    ShellExecute(APE_NULL, APE_NULL, _T("https://www.monkeysaudio.com/help.html"), APE_NULL, APE_NULL, SW_MAXIMIZE);
}

void CMACDlg::OnHelpAbout()
{
    CAboutDlg dlgAbout;
    dlgAbout.DoModal();
}

void CMACDlg::OnHelpWebsiteMonkeysAudio()
{
    ShellExecute(APE_NULL, APE_NULL, _T("https://www.monkeysaudio.com"), APE_NULL, APE_NULL, SW_MAXIMIZE);
}

void CMACDlg::OnHelpWebsiteJRiver()
{
    ShellExecute(APE_NULL, APE_NULL, _T("https://www.jriver.com"), APE_NULL, APE_NULL, SW_MAXIMIZE);
}

void CMACDlg::OnHelpWebsiteWinamp()
{
    ShellExecute(APE_NULL, APE_NULL, _T("https://www.winamp.com"), APE_NULL, APE_NULL, SW_MAXIMIZE);
}

void CMACDlg::OnHelpWebsiteEac()
{
    ShellExecute(APE_NULL, APE_NULL, _T("https://www.exactaudiocopy.de"), APE_NULL, APE_NULL, SW_MAXIMIZE);
}

void CMACDlg::OnHelpLicense()
{
    ShellExecute(APE_NULL, APE_NULL, _T("https://www.monkeysaudio.com/license.html"), APE_NULL, APE_NULL, SW_MAXIMIZE);
}

void CMACDlg::OnToolsOptions()
{
    COptionsDlg dlgOptions(this);
    if (dlgOptions.DoModal() == IDOK)
    {
        // we need to reload the size and font since the scale numbers could change
        // we need to force load since it only checks for a scale change but when the font size changes it also needs to reload
        LoadScale(true);
    }
}

BOOL CMACDlg::PreTranslateMessage(MSG * pMsg)
{
    if (TranslateAccelerator(m_hWnd, m_hAcceleratorTable, pMsg) != 0)
        return true;

    if (pMsg->message == WM_SYSKEYDOWN)
    {
        if (pMsg->wParam == VK_MENU)
        {
            SetMenuShortcutMode(!m_bMenuShortcutMode);
        }
        else if (pMsg->wParam == 'F')
        {
            SetMenuShortcutMode(TRUE);
            OnMenuFile();
        }
        else if (pMsg->wParam == 'M')
        {
            SetMenuShortcutMode(TRUE);
            OnMenuMode();
            return TRUE;
        }
        else if (pMsg->wParam == 'T')
        {
            SetMenuShortcutMode(TRUE);
            OnMenuTools();
            return TRUE;
        }
        else if (pMsg->wParam == 'H')
        {
            SetMenuShortcutMode(TRUE);
            OnMenuHelp();
            return TRUE;
        }
    }
    else if (pMsg->message == WM_SYSKEYUP)
    {
        SetMenuShortcutMode(FALSE);
    }

    return CDialog::PreTranslateMessage(pMsg);
}

void CMACDlg::OnInitMenu(CMenu * pMenu)
{
    CDialog::OnInitMenu(pMenu);
}

void CMACDlg::OnInitMenuPopup(CMenu * pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
    CDialog::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);

    // compression menu
    if (pPopupMenu->GetMenuItemID(0) == ID_COMPRESSION_NA)
    {
        pPopupMenu->RemoveMenu(0, MF_BYPOSITION);
        theApp.GetFormatArray()->FillCompressionMenu(pPopupMenu);
        ChangeMenuToOwnerDraw(pPopupMenu);
    }

    // check the mode
    pPopupMenu->CheckMenuItem(ID_MODE_COMPRESS, ((theApp.GetSettings()->GetMode() == MODE_COMPRESS) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_MODE_DECOMPRESS, ((theApp.GetSettings()->GetMode() == MODE_DECOMPRESS) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_MODE_VERIFY, ((theApp.GetSettings()->GetMode() == MODE_VERIFY) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_MODE_CONVERT, ((theApp.GetSettings()->GetMode() == MODE_CONVERT) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_MODE_MAKE_APL, ((theApp.GetSettings()->GetMode() == MODE_MAKE_APL) ? MF_CHECKED : 0) | MF_BYCOMMAND);

    // check the compression
    pPopupMenu->CheckMenuItem(ID_COMPRESSION_APE_FAST, (((theApp.GetSettings()->GetFormat() == COMPRESSION_APE) && (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_FAST)) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_COMPRESSION_APE_NORMAL, (((theApp.GetSettings()->GetFormat() == COMPRESSION_APE) && (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_NORMAL)) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_COMPRESSION_APE_HIGH, (((theApp.GetSettings()->GetFormat() == COMPRESSION_APE) && (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_HIGH)) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_COMPRESSION_APE_EXTRA_HIGH, (((theApp.GetSettings()->GetFormat() == COMPRESSION_APE) && (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_EXTRA_HIGH)) ? MF_CHECKED : 0) | MF_BYCOMMAND);
    pPopupMenu->CheckMenuItem(ID_COMPRESSION_APE_INSANE, (((theApp.GetSettings()->GetFormat() == COMPRESSION_APE) && (theApp.GetSettings()->GetLevel() == APE_COMPRESSION_LEVEL_INSANE)) ? MF_CHECKED : 0) | MF_BYCOMMAND);

    // enable / disable the compression menu
    bool bEnableCompressionMenu = (theApp.GetSettings()->GetMode() == MODE_COMPRESS) || (theApp.GetSettings()->GetMode() == MODE_CONVERT);
    pPopupMenu->EnableMenuItem(ID_COMPRESSION_APE_FAST, (bEnableCompressionMenu ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
    pPopupMenu->EnableMenuItem(ID_COMPRESSION_APE_NORMAL, (bEnableCompressionMenu ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
    pPopupMenu->EnableMenuItem(ID_COMPRESSION_APE_HIGH, (bEnableCompressionMenu ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
    pPopupMenu->EnableMenuItem(ID_COMPRESSION_APE_EXTRA_HIGH, (bEnableCompressionMenu ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
    pPopupMenu->EnableMenuItem(ID_COMPRESSION_APE_INSANE, (bEnableCompressionMenu ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
}

void CMACDlg::OnGetMinMaxInfo(MINMAXINFO FAR * lpMMI)
{
    lpMMI->ptMinTrackSize.x = 320;
    lpMMI->ptMinTrackSize.y = 240;

    CDialog::OnGetMinMaxInfo(lpMMI);
}

BOOL CMACDlg::OnQueryEndSession()
{
    return true;
}

void CMACDlg::OnEndSession(BOOL)
{
    SendMessage(WM_CLOSE);
    exit(EXIT_SUCCESS);
}

LRESULT CMACDlg::OnDPIChanged(WPARAM, LPARAM lParam)
{
    // call the default window procedure as well (needed so size handling works nicely when we go from one DPI to another)
    Default();

    if (m_bInitialized)
    {
        // load scale
        LoadScale();

        // position (this should perform a layout)
        CRect rect(reinterpret_cast<RECT *>(lParam));
        SetWindowPos(APE_NULL, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // this means we handled the message
    return 0;
}

void CMACDlg::OnListItemChanged(NMHDR *, LRESULT * pResult)
{
    // run on a timer because this gets fired over and over and we just want to update once everything settles down
    SetTimer(IDT_UPDATE_ENBALBED_STATES, 100, APE_NULL);
    *pResult = 0;
}

void CMACDlg::WinHelp(DWORD_PTR, UINT nCmd)
{
    if (nCmd == HELP_CONTEXT)
        OnHelpHelp();
}

int CMACDlg::GetControlHeight(CWnd * pwndControl) const
{
    int nHeight = 0;

    // switch the height depending on the control type
    CString strClass;
    GetClassName(pwndControl->GetSafeHwnd(), strClass.GetBuffer(256), 255);
    strClass.ReleaseBuffer();

    if (strClass.CompareNoCase(_T("combobox")) == 0)
        nHeight = m_nFontHeight + theApp.GetSize(14);
    else if (strClass.CompareNoCase(_T("button")) == 0)
        nHeight = m_nFontHeight + theApp.GetSize(10);
    else if (strClass.CompareNoCase(_T("static")) == 0)
        nHeight = m_nFontHeight;
    else
        nHeight = m_nFontHeight; // catch-all, but hopefully nothing uses this

    return nHeight;
}

void CMACDlg::LayoutControlTop(CWnd * pwndLayout, CRect & rectLayout, bool bOnlyControlWidth, bool bCombobox, CWnd * pwndRight) const
{
    if (pwndLayout == APE_NULL)
        return;

    CRect rectWindow;
    pwndLayout->GetWindowRect(&rectWindow);

    // make a standard height
    rectWindow.bottom = rectWindow.top + GetControlHeight(pwndLayout);

    // layout
    CRect rectRight;
    if (pwndRight != APE_NULL)
    {
        pwndRight->GetWindowRect(&rectRight);
        pwndRight->SetWindowPos(APE_NULL, rectLayout.right - rectRight.Width(), rectLayout.top, rectRight.Width(), GetControlHeight(pwndRight), SWP_NOZORDER);
        rectLayout.right -= rectRight.Width() + theApp.GetSize(8);
    }
    pwndLayout->SetWindowPos(APE_NULL, rectLayout.left, rectLayout.top, bOnlyControlWidth ? rectWindow.Width() : rectLayout.Width(), bCombobox ? theApp.GetSize(500) : rectWindow.Height(), SWP_NOZORDER);
    rectLayout.top += rectWindow.Height(); // control height
    if (bOnlyControlWidth)
        rectLayout.left += rectWindow.Width() + theApp.GetSize(8); // window and border
    else
        rectLayout.top += theApp.GetSize(8); // border
    if (pwndRight != APE_NULL)
    {
        rectLayout.right += rectRight.Width() + theApp.GetSize(8);
    }

    // select nothing
    if (bCombobox)
    {
        CComboBox * pCombo = static_cast<CComboBox *>(pwndLayout);
        pCombo->SetEditSel(0, 0);
    }
}

void CMACDlg::LayoutControlTopWithDivider(CWnd * pwndLayout, CWnd * pwndDivider, CWnd * pwndImage, CRect & rectLayout) const
{
    if (pwndLayout == APE_NULL)
        return;

    // get window sizes
    CRect rectWindow;
    pwndLayout->GetWindowRect(&rectWindow);
    CRect rectDivider;
    pwndDivider->GetWindowRect(&rectDivider);
    CRect rectImage;
    pwndImage->GetWindowRect(&rectImage);

    // size for text
    CString strWindowText;
    pwndLayout->GetWindowText(strWindowText);
    CSize sizeText = theApp.GetTextExtent(pwndLayout, strWindowText);
    rectWindow.right = rectWindow.left + sizeText.cx + theApp.GetSize(8);

    // make a standard height
    rectWindow.bottom = rectWindow.top + GetControlHeight(pwndLayout);

    // layout
    pwndLayout->SetWindowPos(APE_NULL, rectLayout.left, rectLayout.top, rectWindow.Width(), rectWindow.Height(), SWP_NOZORDER);
    pwndDivider->SetWindowPos(APE_NULL, rectLayout.left + rectWindow.Width(), rectLayout.top + (rectWindow.Height() / 2) - (rectDivider.Height() / 2), rectLayout.Width() - rectWindow.Width(), rectDivider.Height(), SWP_NOZORDER);
    pwndImage->SetWindowPos(APE_NULL, rectLayout.left, rectLayout.top + rectWindow.Height() + theApp.GetSize(6), rectImage.Width(), rectImage.Height(), SWP_NOZORDER);
    rectLayout.top += rectWindow.Height(); // control height
    rectLayout.top += theApp.GetSize(8); // border

    rectLayout.left += rectImage.Width() + theApp.GetSize(16);
}

void CMACDlg::PlayDefaultSound()
{
    HRSRC hSound = FindResource(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_COMPLETION_WAVE), _T("WAVE"));
    if (hSound != APE_NULL)
    {
        HGLOBAL hResource = LoadResource(AfxGetInstanceHandle(), hSound);
        if (hResource != APE_NULL)
        {
            LPCTSTR pSound = static_cast<LPCTSTR>(LockResource(hResource));
            sndPlaySound(pSound, SND_MEMORY | SND_ASYNC);
            UnlockResource(hResource);
            FreeResource(hResource);
        }
    }
}

void CMACDlg::LoadScale(bool bForce)
{
    // default to 1.0
    double dScale = 1.0;

    // check the scale
    UINT(STDAPICALLTYPE * pGetDpiForWindow) (IN HWND hwnd) = APE_NULL;
    HMODULE hUser32 = LoadLibrary(_T("user32.dll"));
    if (hUser32 != APE_NULL)
    {
        *(reinterpret_cast<FARPROC *>(&pGetDpiForWindow)) = GetProcAddress(hUser32, "GetDpiForWindow"); // NOLINT
        if (pGetDpiForWindow != APE_NULL)
        {
            theApp.GetMonitorDPI() = pGetDpiForWindow(m_hWnd);
            dScale = static_cast<double>(theApp.GetMonitorDPI()) / static_cast<double>(96.0);
        }

        FreeLibrary(hUser32);
    }

    // save the columns before we switch the scale
    if (m_bInitialized)
        m_ctrlList.SaveColumns();

    // allow changinge with the user setting
    double dSize = theApp.GetSettings()->LoadSetting(_T("Size"), 100);
    dScale = (dScale * dSize) / 100.0;

    // set the scale
    //dScale = 2.0; // test for high scales
    //dScale = 3.0;
    //dScale = 4.0;
    if (theApp.SetScale(dScale, bForce) == false)
    {
        if (bForce == false)
            return; // do nothing if the scale didn't change
    }

    // get the text height (greater than the log font height)
    CPaintDC dc(this);
    dc.SelectObject(theApp.GetFont());
    TEXTMETRIC Metrics; APE_CLEAR(Metrics);
    dc.GetTextMetrics(&Metrics);
    m_nFontHeight = Metrics.tmHeight;

    // toolbar
    if (m_ctrlToolbar.GetSafeHwnd() == APE_NULL)
    {
        m_ctrlToolbar.Create(this, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_BORDER_BOTTOM | TBSTYLE_FLAT);
        INITIALIZE_COMMON_CONTROL(m_ctrlToolbar.GetSafeHwnd())
        m_ctrlToolbar.ModifyStyle(0, TBSTYLE_FLAT);
    }

    // status bar
    if (m_ctrlStatusBar.GetSafeHwnd() == APE_NULL)
    {
        m_ctrlStatusBar.Create(this);
        INITIALIZE_COMMON_CONTROL(m_ctrlStatusBar.GetSafeHwnd())
    }

    // set images
    m_ctrlToolbar.GetToolBarCtrl().SetExtendedStyle(TBSTYLE_EX_DRAWDDARROWS);
    m_ctrlToolbar.GetToolBarCtrl().SetImageList(theApp.GetImageList(CMACApp::Image_Toolbar));
    m_ctrlToolbar.GetToolBarCtrl().SetDisabledImageList(theApp.GetImageList(CMACApp::Image_ToolbarDisabled));

    // recreate the menu
    CreateMenu();

    // set the font to all the controls
    {
        m_ctrlList.SetFont(theApp.GetFont());
        m_ctrlStatusBar.SetFont(theApp.GetFont());
        m_ctrlToolbar.SetFont(theApp.GetFont());
        SetFont(theApp.GetFont());
    }

    // load menu and toolbar
    LoadMenuAndToolbar(GetProcessing(), true);

    // load columns
    m_ctrlList.LoadColumns();

    // size the toolbar (using the size that it lays out at plus the icon size)
    // use the font size of a string used in the toolbar plus some constants to size (this works better than using the size logic built into the toolbar on 4/2/2026)
    CSize sizeText = theApp.GetTextExtent(this, _T("Remove Files"));

    // set uniform widths so the buttons don't change size when we change the text
    int nButtonWidth = sizeText.cx + theApp.GetSize(4);
    m_ctrlToolbar.GetToolBarCtrl().SetButtonWidth(nButtonWidth, nButtonWidth);
    m_ctrlToolbar.GetToolBarCtrl().AutoSize();

    // resize to get the right toolbar height
    LayoutWindow();
}

void CMACDlg::FilesChanged(MAC_FILE_ARRAY * paryFiles)
{
    if (m_ctrlStatusBar.m_hWnd != APE_NULL)
        m_ctrlStatusBar.UpdateFiles(paryFiles);

    UpdateEnabledStates();
}

void CMACDlg::UpdateEnabledStates()
{
    // analyze selection
    bool bHasFiles = (m_ctrlList.GetItemCount() > 0);
    bool bHasMultipleFiles = (m_ctrlList.GetItemCount() > 1);
    POSITION posListSelection = m_ctrlList.GetFirstSelectedItemPosition();
    int nListSelectionCount = 0;
    while (posListSelection != APE_NULL)
    {
        m_ctrlList.GetNextSelectedItem(posListSelection);
        nListSelectionCount++;
    }
    bool bHasSelection = (nListSelectionCount > 0);
    bool bHasMultipleSelection = (nListSelectionCount > 1);

    // set the state of the toolbar and menu items
    m_ctrlToolbar.GetToolBarCtrl().EnableButton(ID_FILE_REMOVE_FILES, bHasSelection);
    m_menuMain.EnableMenuItem(ID_FILE_REMOVE_FILES, bHasSelection ? 0 : MF_GRAYED);

    m_ctrlToolbar.GetToolBarCtrl().EnableButton(ID_FILE_CLEAR_FILES, bHasFiles);
    m_menuMain.EnableMenuItem(ID_FILE_CLEAR_FILES, bHasFiles ? 0 : MF_GRAYED);

    m_menuMain.EnableMenuItem(ID_FILE_SELECTALL, bHasFiles ? 0 : MF_GRAYED);

    m_menuMain.EnableMenuItem(ID_FILE_PROCESS_FILES, bHasFiles ? 0 : MF_GRAYED);

    m_ctrlToolbar.GetToolBarCtrl().EnableButton(ID_FILE_FILE_INFO, bHasFiles);
    m_menuMain.EnableMenuItem(ID_FILE_FILE_INFO, bHasFiles ? 0 : MF_GRAYED);

    // update the process files and remove files text based on one or many for the menu
    MENUITEMINFO MenuInfo;
    APE_CLEAR(MenuInfo);
    MenuInfo.cbSize = sizeof(MENUITEMINFO);
    MenuInfo.fMask = MIIM_STRING;
    wchar_t aryTypeData[1024];
    APE_CLEAR(aryTypeData);
    MenuInfo.dwTypeData = aryTypeData; // use type data pointer and copy the strings into this buffer

    wcscpy_s(aryTypeData, 1024, bHasMultipleFiles ? L"Process Files\tEnter" : L"Process File\tEnter");
    m_menuMain.SetMenuItemInfo(ID_FILE_PROCESS_FILES, &MenuInfo, 0);

    wcscpy_s(aryTypeData, 1024, bHasMultipleSelection ? L"Remove Files\tCtrl+R" : L"Remove File\tCtrl+R");
    m_menuMain.SetMenuItemInfo(ID_FILE_REMOVE_FILES, &MenuInfo, 0);

    // update toolbar button for remove files
    int nRemoveIndex = m_ctrlToolbar.CommandToIndex(ID_FILE_REMOVE_FILES);
    if (nRemoveIndex >= 0)
        m_ctrlToolbar.SetButtonText(nRemoveIndex, bHasMultipleSelection ? _T("Remove Files") : _T("Remove File"));

    // redraw the toolbar because it changes size when we switch the text
    m_ctrlToolbar.Invalidate();
}

void CMACDlg::OnMeasureItem(int, LPMEASUREITEMSTRUCT lpMIS)
{
    if (lpMIS->CtlType == ODT_MENU)
    {
        MENUITEMINFO mii; CString strText;
        GetMenuInfo(lpMIS->itemID, mii, strText);

        if (mii.fType & MFT_SEPARATOR)
        {
            // divider
            lpMIS->itemHeight = static_cast<UINT>(theApp.GetSize(4));
        }
        else
        {
            // menu item
            strText.Replace(_T("\t"), _T(" "));
            strText.Replace(_T("&"), _T(""));
            CSize sizeText = theApp.GetTextExtent(this, strText);

            // height
            lpMIS->itemHeight = static_cast<UINT>(sizeText.cy + MENU_BORDER + MENU_BORDER + MENU_EXTRA_HEIGHT);

            // width
            int nItemWidth = 0;
            nItemWidth += MENU_BORDER + MENU_BORDER + static_cast<int>(lpMIS->itemHeight); // checkbox
            nItemWidth += sizeText.cx; // text
            nItemWidth += MENU_RIGHT_BORDER; // right border
            lpMIS->itemWidth = static_cast<UINT>(nItemWidth);
        }
    }
}

void CMACDlg::OnDrawItem(int, LPDRAWITEMSTRUCT lpDIS)
{
    if (lpDIS->CtlType == ODT_MENU)
    {
        // attach a DC
        CDC dc;
        dc.Attach(lpDIS->hDC);
        CRect rc(lpDIS->rcItem);

        // get colors
        COLORREF rgbMenuBackground = 0, rgbMenuHighlight = 0, rgbMenuText = 0, rgbMenuGrayText = 0, rgbMenuHighlightText = 0, rgbDivider = 0;
        theApp.GetMenuColors(rgbMenuBackground, rgbMenuHighlight, rgbMenuText, rgbMenuGrayText, rgbMenuHighlightText, rgbDivider);

        // check for grayed
        bool bGrayed = (lpDIS->itemState & ODS_GRAYED) || (lpDIS->itemState & ODS_DISABLED);

        // select font
        dc.SelectObject(theApp.GetFont());

        // get menu info
        MENUITEMINFO mii; CString strText;
        GetMenuInfo(lpDIS->itemID, mii, strText);

        // draw background
        if ((lpDIS->itemState & ODS_SELECTED) && !(mii.fType & MFT_SEPARATOR))
            dc.FillSolidRect(rc, rgbMenuHighlight); // highlight color
        else
            dc.FillSolidRect(rc, rgbMenuBackground); // normal color

        if (mii.fType & MFT_SEPARATOR)
        {
            // draw divider
            dc.Draw3dRect(rc.left + theApp.GetSize(10), rc.top + rc.Height() / 2, rc.Width() - theApp.GetSize(20), 1, rgbDivider, rgbDivider);
        }
        else
        {
            if (strText.IsEmpty() == false)
            {
                // crop borders
                rc.DeflateRect(MENU_BORDER, MENU_BORDER, MENU_BORDER + MENU_RIGHT_BORDER, MENU_BORDER);

                // set drawing mode
                dc.SetBkMode(TRANSPARENT);
                if (bGrayed)
                    dc.SetTextColor(rgbMenuGrayText);
                else if ((lpDIS->itemState & ODS_SELECTED) && !(mii.fType & MFT_SEPARATOR))
                    dc.SetTextColor(rgbMenuHighlightText);
                else
                    dc.SetTextColor(rgbMenuText);

                bool bUnderline = (GetKeyState(VK_MENU) & 0x8000) ? true : false;

                // draw check
                CRect rectCheck = rc;
                rectCheck.right = rectCheck.left + rc.Height(); // use the height as the width so the checkmark is square

                if ((lpDIS->itemState & ODS_CHECKED) || (mii.fState & MFS_CHECKED))
                {
                    HICON hIcon = theApp.GetCheckIcon();
                    if (hIcon != APE_NULL)
                    {
                        DrawIconEx(dc.GetSafeHdc(), rectCheck.left, rectCheck.top, hIcon, rectCheck.Width(), rectCheck.Height(), 0, APE_NULL, DI_NORMAL);
                    }
                }

                // start the left just to the right of the checkmark
                rc.left = rectCheck.right + MENU_BORDER;

                // split text and draw
                int nTab = strText.Find(_T("\t"));
                if (nTab >= 0)
                {
                    CString strLeft = strText.Left(nTab);
                    CString strRight = strText.Right(strText.GetLength() - nTab - 1);

                    if (bUnderline == false)
                        strLeft.Replace(_T("&"), _T(""));

                    dc.DrawText(strLeft, rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    dc.DrawText(strRight, rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    if (bUnderline == false)
                        strText.Replace(_T("&"), _T(""));

                    dc.DrawText(strText, rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }

        // detach the device context
        dc.Detach();
    }
}

void CMACDlg::OnClose()
{
    if (GetProcessing())
    {
        if (AfxMessageBox(_T("Please confirm that you want to cancel the running operation and close the program."), MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }

    DestroyMenus();

    if (m_hMouseHook != APE_NULL)
    {
        ::UnhookWindowsHookEx(m_hMouseHook);
        m_hMouseHook = APE_NULL;
    }

    CDialog::OnClose();
}

void CMACDlg::GetMenuInfo(UINT nID, MENUITEMINFO & mi, CString & strText)
{
    APE_CLEAR(mi);
    mi.cbSize = sizeof(MENUITEMINFO);
    mi.fMask = MIIM_FTYPE | MIIM_STRING;

    strText.Empty();

    // loop all the program menus
    CMenu * pmenuFound = APE_NULL;
    if (m_menuMain.GetMenuItemInfo(nID, &mi, FALSE))
    {
        pmenuFound = &m_menuMain;
    }
    else
    {
        if (m_pTrackingMenu && m_pTrackingMenu->GetMenuItemInfo(nID, &mi, FALSE))
        {
            pmenuFound = m_pTrackingMenu;
        }
        else
        {
            for (int z = 0; z < theApp.GetFormatArray()->m_aryMenus.GetSize(); z++)
            {
                if (theApp.GetFormatArray()->m_aryMenus[z]->GetMenuItemInfo(nID, &mi, FALSE))
                {
                    pmenuFound = theApp.GetFormatArray()->m_aryMenus[z];
                    break;
                }
            }
        }
    }

    // get the text
    if (mi.cch > 0)
    {
        mi.dwTypeData = strText.GetBuffer(static_cast<int>(mi.cch) + 1);
        mi.cch++; // account for null terminator
        pmenuFound->GetMenuItemInfo(nID, &mi, FALSE);
        strText.ReleaseBuffer();
    }
}

void CMACDlg::ChangeMenuToOwnerDraw(CMenu * pMenu, int nLevel)
{
    int nMenuCount = pMenu->GetMenuItemCount();
    for (int i = 0; i < nMenuCount; i++)
    {
        CMenu * pSubMenu = pMenu->GetSubMenu(i);
        if (pSubMenu)
        {
            // recurse into the submenu
            ChangeMenuToOwnerDraw(pSubMenu, nLevel + 1);

            // modify the popup menu itself to be owner-drawn
            if (nLevel > 0)
            {
                pMenu->ModifyMenu(i, MF_BYPOSITION | MF_POPUP | MF_OWNERDRAW, reinterpret_cast<UINT_PTR>(pSubMenu->GetSafeHmenu()), _T(""));
            }
        }
        else
        {
            // modify individual menu item to be owner-drawn
            if (nLevel > 0)
            {
                MENUITEMINFO MenuItemInfo;
                APE_CLEAR(MenuItemInfo);
                MenuItemInfo.cbSize = sizeof(MenuItemInfo);
                MenuItemInfo.fMask = MIIM_FTYPE;
                pMenu->GetMenuItemInfoW(static_cast<UINT>(i), &MenuItemInfo, TRUE);
                MenuItemInfo.fType |= MF_OWNERDRAW;
                pMenu->SetMenuItemInfoW(static_cast<UINT>(i), &MenuItemInfo, TRUE);
            }
        }
    }
}

void CMACDlg::SetMenuShortcutMode(BOOL bMenuShortcutMode)
{
    if (GetProcessing() || (m_aryMenus.GetSize() != 4))
        return;

    if (bMenuShortcutMode)
    {
        m_aryMenus[0]->SetWindowText(_T("&File"));
        m_aryMenus[1]->SetWindowText(_T("&Mode"));
        m_aryMenus[2]->SetWindowText(_T("&Tools"));
        m_aryMenus[3]->SetWindowText(_T("&Help"));
        m_bMenuShortcutMode = TRUE;
    }
    else
    {
        m_bMenuShortcutMode = FALSE;
        m_aryMenus[0]->SetWindowText(_T("File"));
        m_aryMenus[1]->SetWindowText(_T("Mode"));
        m_aryMenus[2]->SetWindowText(_T("Tools"));
        m_aryMenus[3]->SetWindowText(_T("Help"));
    }
}

LRESULT CMACDlg::MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (wParam == WM_MOUSEMOVE)
    {
        MOUSEHOOKSTRUCT * pMouse = reinterpret_cast<MOUSEHOOKSTRUCT *>(lParam);
        // pMouse->pt contains the current screen coordinates of the mouse

        // only switch menus if we're currently showing a menu
        if ((m_nShowingMenu != -1) && (GetProcessing() == false))
        {
            // loop each menu item
            for (int z = 0; z < m_aryMenus.GetSize(); z++)
            {
                // get window rectangle
                CRect rectMenu;
                m_aryMenus[z]->GetWindowRect(&rectMenu);

                // if we're in the menu and it's different than what we're showing
                if (rectMenu.PtInRect(pMouse->pt) && (z != m_nShowingMenu))
                {
                    // close the menu
                    SendMessage(WM_CANCELMODE);

                    // show the menu on a timer (doing it directly doesn't work)
                    m_nTimerMenuShow = z;
                    SetTimer(IDT_SHOW_MENU, 10, APE_NULL);

                    // quit
                    break;
                }
            }
        }
    }

    return ::CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
}


void CMACDlg::ShowMenu(int nMenu, bool bCheckForRecentHide)
{
    if ((nMenu < 0) || (nMenu >= m_aryMenus.GetSize()))
        return;

    if (bCheckForRecentHide)
    {
        if (m_timerMenuHide.GetElapsedSeconds() < 0.2)
            return;
    }

    m_nShowingMenu = nMenu;
    CRect rectButton;
    if (GetProcessing())
        m_aryMenus[nMenu]->GetWindowRect(&rectButton);
    else
        m_aryMenus[nMenu]->GetWindowRect(&rectButton);
    int nRetVal = m_menuMain.GetSubMenu(nMenu)->TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL | TPM_RETURNCMD,
        rectButton.left, rectButton.bottom, this, &rectButton);
    m_timerMenuHide.Reset();
    if (nRetVal != 0)
        PostMessage(WM_COMMAND, nRetVal, 0);
    m_nShowingMenu = -1;
}

void CMACDlg::CreateMenu()
{
    DestroyMenus();

    if (GetProcessing())
    {
        CStatic * pStatic = new CStatic;
        pStatic->Create(_T("Stop"), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY | SS_CENTERIMAGE, CRect(0, 0, 0, 0), this, ID_MENU_STOP);
        m_aryMenus.Add(pStatic);

        pStatic = new CStatic;
        pStatic->Create(_T("Pause"), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY | SS_CENTERIMAGE, CRect(0, 0, 0, 0), this, ID_MENU_PAUSE);
        m_aryMenus.Add(pStatic);
    }
    else
    {
        CStatic * pStatic = new CStatic;
        pStatic->Create(_T("File"), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY | SS_CENTERIMAGE, CRect(0, 0, 0, 0), this, ID_MENU_FILE);
        m_aryMenus.Add(pStatic);

        pStatic = new CStatic;
        pStatic->Create(_T("Mode"), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY | SS_CENTERIMAGE, CRect(0, 0, 0, 0), this, ID_MENU_MODE);
        m_aryMenus.Add(pStatic);

        pStatic = new CStatic;
        pStatic->Create(_T("Tools"), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY | SS_CENTERIMAGE, CRect(0, 0, 0, 0), this, ID_MENU_TOOLS);
        m_aryMenus.Add(pStatic);

        pStatic = new CStatic;
        pStatic->Create(_T("Help"), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY | SS_CENTERIMAGE, CRect(0, 0, 0, 0), this, ID_MENU_HELP);
        m_aryMenus.Add(pStatic);
    }

    for (int z = 0; z < m_aryMenus.GetSize(); z++)
        m_aryMenus[z]->SetFont(theApp.GetFont());
}

void CMACDlg::DestroyMenus()
{
    for (int z = 0; z < m_aryMenus.GetSize(); z++)
    {
        m_aryMenus[z]->DestroyWindow();
        delete m_aryMenus[z];
    }
    m_aryMenus.RemoveAll();
}
