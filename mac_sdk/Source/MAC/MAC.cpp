#include "stdafx.h"
#include "MAC.h"
#include "MACDlg.h"
#include "FormatArray.h"
#include "MACSettings.h"
#include "APEButtons.h"
#include "GlobalFunctions.h"

using namespace APE;

// the global application object
CMACApp theApp;

BEGIN_MESSAGE_MAP(CMACApp, CWinApp)
    ON_COMMAND(ID_HELP, &CMACApp::OnHelp)
END_MESSAGE_MAP()

CMACApp::CMACApp() noexcept
{
    // initialize
    m_dScale = -1.0; // default to an impossible value so the first SetScale(...) call returns that it changed
    m_hSingleInstance = CreateMutex(APE_NULL, false, _T("Mokey's Audio"));
    DWORD dwLastError = GetLastError();
    m_bAnotherInstanceRunning = (dwLastError == ERROR_ALREADY_EXISTS);
    m_pMACDlg = APE_NULL;
    m_nMonitorDPI = 0;
    m_hCheckIcon = APE_NULL;

    // turn on high DPI support (also done in the manifest)
    EnableHighDPISupport();

    // start GDI plus
    Gdiplus::GdiplusStartupInput gdiplusStartupInput; // filled in by the constructor
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, APE_NULL);

    // initialize common controls
    INITCOMMONCONTROLSEX icex;
    APE_CLEAR(icex);
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);
}

CMACApp::~CMACApp()
{
    // everything should have gotten cleaned up in ExitInstance
}

BOOL CMACApp::InitInstance()
{
    CWinApp::InitInstance();

    // don't run if we're already running
    if (m_bAnotherInstanceRunning)
    {
        HWND hwndMonkeysAudio = FindWindow(APE_NULL, APE_NAME);
        if (hwndMonkeysAudio != APE_NULL)
        {
            ShowWindow(hwndMonkeysAudio, SW_RESTORE);
            SetForegroundWindow(hwndMonkeysAudio);
        }
        return false;
    }

    // initialize
    AfxEnableControlContainer();
    AfxOleInit();
    InitCommonControls();
    AfxInitRichEdit();

    // parse command line
    CString strCommandLine = GetCommandLine();
    //AfxMessageBox(strCommandLine);
    strCommandLine = strCommandLine.Right(strCommandLine.GetLength() - (strCommandLine.Find(_T(".exe")) + 4));
    strCommandLine.TrimLeft(_T(" \"")); strCommandLine.TrimRight(_T(" \""));

    // files
    CStringArrayEx aryFiles;

    // uninstall if specified
    if (strCommandLine.CompareNoCase(_T("-uninstall")) == 0)
    {
        // launch the uninstall (this way we show as the icon for the uninstall instead of a non-descript one)
        TCHAR * pUninstall = new TCHAR [APE_MAX_PATH];
        pUninstall[0] = 0;
        _tcscat_s(pUninstall, APE_MAX_PATH, GetInstallPath());
        _tcscat_s(pUninstall, APE_MAX_PATH, _T("uninstall.exe"));

        ShellExecute(APE_NULL, APE_NULL, pUninstall, APE_NULL, APE_NULL, SW_SHOW);

        APE_SAFE_ARRAY_DELETE(pUninstall)

        return false;
    }
    else
    {
        // assume we got a list of files
        aryFiles.InitFromList(strCommandLine, _T("|"));
    }

    // show program
    CMACDlg dlg(&aryFiles);
    m_pMACDlg = &dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();
    if (nResponse == IDOK)
    {
    }
    else if (nResponse == IDCANCEL)
    {
    }
    m_pMACDlg = APE_NULL;

    // Since the dialog has been closed, return FALSE so that we exit the
    //  application, rather than start the application's message pump.
    return false;
}

int CMACApp::ExitInstance()
{
    // destroy everything
    DestroyObjects();

    // close single instance handle
    if (m_hSingleInstance != APE_NULL)
    {
        CloseHandle(m_hSingleInstance);
        m_hSingleInstance = APE_NULL;
    }

    // delete helpers
    m_sparyFormats.Delete();
    m_spSettings.Delete();

    return CWinApp::ExitInstance();
}

CFormatArray * CMACApp::GetFormatArray()
{
    if (m_sparyFormats == APE_NULL)
        m_sparyFormats.Assign(new CFormatArray(m_pMACDlg));
    return m_sparyFormats.GetPtr();
}

CMACSettings * CMACApp::GetSettings()
{
    if (m_spSettings == APE_NULL)
        m_spSettings.Assign(new CMACSettings);
    return m_spSettings.GetPtr();
}

CImageList * CMACApp::GetImageList(EImageList Image)
{
    CImageList * pImageList = (Image == Image_Toolbar) ? &m_ImageListToolbar : (Image == Image_ToolbarDisabled) ? &m_ImageListToolbarDisabled : (Image == Image_OptionsList) ? &m_ImageListOptionsList : (Image == Image_OptionsPages) ? &m_ImageListOptionsPages : (Image == Image_Menu) ? &m_ImageListMenu : APE_NULL;
    if ((pImageList != APE_NULL) && (pImageList->GetSafeHandle() == APE_NULL))
    {
        // load the image
        if (m_spbmpButtons == APE_NULL)
        {
            CString strImage = GetProgramPath() + _T("APE_Buttons.png");
            if (FileExists(strImage) == false)
                strImage = GetInstallPath() + _T("APE_Buttons.png");
            m_spbmpButtons.Assign(Gdiplus::Bitmap::FromFile(strImage));
        }
        if ((m_spbmpButtons == APE_NULL) || (m_spbmpButtons->GetLastStatus() != Gdiplus::Ok))
            return APE_NULL;

        // build the image list
        int nSizeImageList = GetSize(32);
        if (Image == Image_Menu)
        {
            // the approximate size of the check
            int nHeight = theApp.GetTextExtent(m_pMACDlg, _T("Ay")).cy;
            nHeight += MENU_EXTRA_HEIGHT;
            nSizeImageList = nHeight;
        }
        pImageList->Create(nSizeImageList, nSizeImageList, ILC_COLOR32, TBB_COUNT + 1, 10);

        // setup drawing
        int nImages = static_cast<int>(m_spbmpButtons->GetWidth() / m_spbmpButtons->GetHeight());
        Gdiplus::Bitmap newBmp(nSizeImageList * nImages, nSizeImageList, PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(&newBmp);
        graphics.SetInterpolationMode(Gdiplus::InterpolationMode::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // fill with the background color of the toolbar
        if ((Image == Image_Toolbar) || (Image == Image_OptionsPages))
        {
            Gdiplus::SolidBrush b(Gdiplus::Color(255, 240, 240, 240));
            graphics.FillRectangle(&b, 0, 0, TBB_COUNT * nSizeImageList, nSizeImageList);
        }
        else if (Image == Image_OptionsList)
        {
            Gdiplus::SolidBrush b(Gdiplus::Color(255, 255, 255, 255));
            graphics.FillRectangle(&b, 0, 0, TBB_COUNT * nSizeImageList, nSizeImageList);
        }
        else if (Image == Image_Menu)
        {
            COLORREF rgbMenuBackground = 0, rgbMenuHighlight = 0, rgbMenuText = 0, rgbMenuGrayText = 0, rgbMenuHighlightText = 0, rgbDivider = 0;
            GetMenuColors(rgbMenuBackground, rgbMenuHighlight, rgbMenuText, rgbMenuGrayText, rgbMenuHighlightText, rgbDivider);

            Gdiplus::SolidBrush b(rgbMenuBackground);
            graphics.FillRectangle(&b, 0, 0, TBB_COUNT * nSizeImageList, nSizeImageList);
        }

        // draw the image over the top
        int nSizeBitmap = static_cast<int>(m_spbmpButtons->GetHeight());
        Gdiplus::Bitmap bmpSingle(nSizeBitmap, nSizeBitmap, PixelFormat32bppPARGB);
        Gdiplus::Graphics graphicsSingle(&bmpSingle);
        graphicsSingle.SetInterpolationMode(Gdiplus::InterpolationMode::InterpolationModeHighQualityBicubic);
        graphicsSingle.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        for (int z = 0; z < TBB_COUNT; z++)
        {
            // only do certain images in some cases
            if (Image == Image_Menu)
            {
                if (z != TBB_CHECKMARK)
                    continue;
            }

            // copy the individual image to the single buffer
            // we do this because there were artifacts drawing from an image that had all the images next to each other
            // the artifacts were found 12/3/2021 and was a line next to the Make APL icon
            graphicsSingle.Clear(Gdiplus::Color(0));
            graphicsSingle.DrawImage(m_spbmpButtons, Gdiplus::Rect(0, 0, nSizeBitmap, nSizeBitmap), (nSizeBitmap * z), 0, nSizeBitmap, nSizeBitmap, Gdiplus::UnitPixel);

            // make the Image_ToolbarDisabled image disabled
            if (Image == Image_ToolbarDisabled)
            {
                for (int y = 0; y < static_cast<int>(bmpSingle.GetHeight()); y++)
                {
                    for (int x = 0; x < static_cast<int>(bmpSingle.GetWidth()); x++)
                    {
                        // get pixel
                        Gdiplus::Color clrPixel;
                        bmpSingle.GetPixel(x, y, &clrPixel);

                        // calculate weighted average for grayscale
                        BYTE nGray = static_cast<BYTE>(0.299 * clrPixel.GetRed() +
                            0.587 * clrPixel.GetGreen() +
                            0.114 * clrPixel.GetBlue());

                        // create pixel using grayscale
                        bmpSingle.SetPixel(x, y, Gdiplus::Color(clrPixel.GetAlpha(), nGray, nGray, nGray));
                    }
                }
            }

            // draw onto the graphics object
            Gdiplus::Rect rectSource(0, 0, nSizeBitmap, nSizeBitmap);
            Gdiplus::Rect rectDestination(z * nSizeImageList, 0, nSizeImageList, nSizeImageList);
            graphics.DrawImage(&bmpSingle, rectDestination, rectSource.X, rectSource.Y, rectSource.Width, rectSource.Height, Gdiplus::UnitPixel);
        }

        // get the bitmap
        HBITMAP hBitmap;
        newBmp.GetHBITMAP(Gdiplus::Color::Transparent, &hBitmap);

        // add to the image list
        ImageList_AddMasked(pImageList->GetSafeHandle(), hBitmap, 0);
    }

    return pImageList;
}

HICON CMACApp::GetCheckIcon()
{
    if (m_hCheckIcon == APE_NULL)
    {
        CImageList * pCheckImageList = theApp.GetImageList(CMACApp::Image_Menu);
        m_hCheckIcon = pCheckImageList->ExtractIcon(TBB_CHECKMARK);
    }
    return m_hCheckIcon;
}

int CMACApp::GetSize(int nSize, double dAdditional) const
{
    int nSizeFinished = static_cast<int>(m_dScale * dAdditional * static_cast<double>(nSize));
    return nSizeFinished;
}

int CMACApp::GetSizeReverse(int nSize) const
{
    int nNewSize = static_cast<int>(static_cast<double>(nSize) / m_dScale);
    return nNewSize;
}

CSize CMACApp::GetTextExtent(CWnd * pWnd, const CString & strText, CFont * pFont)
{
    CSize sizeText(0, 0);

    CDC * pDC = pWnd->GetDC();
    if (pDC != APE_NULL)
    {
        if (pFont == APE_NULL)
            pFont = GetFont();
        pDC->SelectObject(pFont);
        sizeText = pDC->GetTextExtent(strText);
        pWnd->ReleaseDC(pDC);
    }

    return sizeText;
}

bool CMACApp::SetScale(double dScale, bool bForce)
{
    bool bResult = false;
    if (bForce || (fabs(dScale - m_dScale) > 0.01))
    {
        // store scale
        m_dScale = dScale;

        // reset everyting
        DestroyObjects();

        // build the font again
        LoadFont();

        // success
        bResult = true;
    }
    return bResult;
}

Gdiplus::Bitmap * CMACApp::GetMonkeyImage()
{
    if (m_spbmpMonkey == APE_NULL)
    {
        // load image
        CString strImage = GetProgramPath() + _T("Monkey.png");
        if (FileExists(strImage) == false)
            strImage = GetInstallPath() + _T("Monkey.png");
        m_spbmpMonkey.Assign(Gdiplus::Bitmap::FromFile(strImage));

        // lock bits and scale alpha
        Gdiplus::BitmapData bitmapData;
        Gdiplus::Rect rectLock(0, 0, static_cast<INT>(m_spbmpMonkey->GetWidth()), static_cast<INT>(m_spbmpMonkey->GetHeight()));
        m_spbmpMonkey->LockBits(&rectLock, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bitmapData);
        uint32 * pPixel = static_cast<uint32 *>(bitmapData.Scan0);
        for (int nPixel = 0; nPixel < int(bitmapData.Width * bitmapData.Height); nPixel++)
        {
            uint32 nAlpha = pPixel[nPixel] >> 24;
            nAlpha /= 8; // scale alpha down so the image is faded
            pPixel[nPixel] = (nAlpha << 24) | (pPixel[nPixel] & 0x00FFFFFF);
        }
        m_spbmpMonkey->UnlockBits(&bitmapData);
    }
    return m_spbmpMonkey;
}

void CMACApp::LoadFont()
{
    // reset if we already exist
    if (m_Font.GetSafeHandle() != APE_NULL)
        m_Font.DeleteObject();

    // load default sized font
    LoadFont(m_Font, 100);
}

void CMACApp::LoadFont(CFont & Font, int nSizeScale, bool bBold)
{
    // get the system font using SPI_GETNONCLIENTMETRICS
    NONCLIENTMETRICS ncm;
    APE_CLEAR(ncm);
    ncm.cbSize = sizeof(ncm);

    // try to get DPI aware
    BOOL bResult = false;
    HMODULE hUser32 = LoadLibrary(_T("user32.dll"));
    if (hUser32 != APE_NULL)
    {
        BOOL(STDAPICALLTYPE * pSystemParametersInfoForDpi) (IN UINT uiAction, IN UINT uiParam, PVOID pvParam, IN UINT fWinIni, IN UINT dpi) = APE_NULL;
        *(reinterpret_cast<FARPROC *>(&pSystemParametersInfoForDpi)) = GetProcAddress(hUser32, "SystemParametersInfoForDpi"); // NOLINT
        if (pSystemParametersInfoForDpi != APE_NULL)
        {
            bResult = pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0, m_nMonitorDPI);
        }

        FreeLibrary(hUser32);
    }

    // get non-DPI aware
    if (bResult == false)
    {
        // get font
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);

        // scale by scale factor (since we're not DPI aware)
        ncm.lfMessageFont.lfHeight = GetSize(ncm.lfMessageFont.lfHeight);
    }

    // scale by the setting
    double dSize = static_cast<double>(GetSettings()->LoadSetting(_T("Font Size"), 100));
    if (nSizeScale != 100)
        dSize = (dSize * static_cast<double>(nSizeScale)) / 100;
    ncm.lfMessageFont.lfHeight = static_cast<LONG>((static_cast<double>(ncm.lfMessageFont.lfHeight) * dSize) / 100.0);

    // bold
    if (bBold)
        ncm.lfMessageFont.lfWeight = FW_BOLD;

    // build font
    Font.CreateFontIndirect(&ncm.lfMessageFont);
}

void CMACApp::GetMenuColors(COLORREF & rgbMenuBackground, COLORREF & rgbMenuHighlight, COLORREF & rgbMenuText, COLORREF & rgbMenuGrayText, COLORREF & rgbMenuHighlightText, COLORREF & rgbDivider)
{
    HTHEME hTheme = OpenThemeData(m_pMACDlg ? m_pMACDlg->GetSafeHwnd() : APE_NULL, L"WINDOW");
    if (hTheme != APE_NULL)
    {
        rgbMenuBackground = GetThemeSysColor(hTheme, COLOR_MENU);
        rgbMenuHighlight = GetThemeSysColor(hTheme, COLOR_HIGHLIGHT);
        rgbMenuText = GetThemeSysColor(hTheme, COLOR_MENUTEXT);
        rgbMenuGrayText = GetThemeSysColor(hTheme, COLOR_GRAYTEXT);
        rgbMenuHighlightText = GetThemeSysColor(hTheme, COLOR_HIGHLIGHTTEXT);
        rgbDivider = GetThemeSysColor(hTheme, COLOR_BTNSHADOW);
    }
    else
    {
        rgbMenuBackground = GetSysColor(COLOR_MENU);
        rgbMenuHighlight = GetSysColor(COLOR_HIGHLIGHT);
        rgbMenuText = GetSysColor(COLOR_MENUTEXT);
        rgbMenuGrayText = GetSysColor(COLOR_GRAYTEXT);
        rgbMenuHighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        rgbDivider = GetSysColor(COLOR_BTNSHADOW);
    }
}

void CMACApp::DestroyObjects()
{
    // reset the font
    m_Font.DeleteObject();

    // reset images
    m_ImageListToolbar.DeleteImageList();
    m_ImageListToolbarDisabled.DeleteImageList();
    m_ImageListOptionsList.DeleteImageList();
    m_ImageListOptionsPages.DeleteImageList();
    m_ImageListMenu.DeleteImageList();

    // reset images
    m_spbmpButtons.Delete();
    m_spbmpMonkey.Delete();

    // release icon
    if (m_hCheckIcon != APE_NULL)
    {
        DestroyIcon(m_hCheckIcon);
        m_hCheckIcon = APE_NULL;
    }
}
