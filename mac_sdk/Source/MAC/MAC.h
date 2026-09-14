#pragma once

class CFormatArray;
class CMACSettings;
class CMACDlg;

#pragma warning(push)
#include <gdiplus.h>
#pragma warning(pop)

#include "resource.h"
#include "GDIBitmapPtr.h"

class CMACApp : public CWinApp
{
public:
    // construction / destruction
    CMACApp() noexcept;
    ~CMACApp() APE_OVERRIDE;

    // initialize
    virtual BOOL InitInstance() APE_OVERRIDE;
    virtual int ExitInstance() APE_OVERRIDE;

    // data access
    CFormatArray * GetFormatArray();
    CMACSettings * GetSettings();
    enum EImageList
    {
        Image_Toolbar,
        Image_ToolbarDisabled,
        Image_OptionsList,
        Image_OptionsPages,
        Image_Menu
    };
    CImageList * GetImageList(EImageList Image);
    HICON GetCheckIcon();
    int GetSize(int nSize, double dAdditional = 1.0) const;
    int GetSizeReverse(int nSize) const;
    double GetScale() const { return m_dScale; }
    CFont * GetFont() { return &m_Font; }
    CSize GetTextExtent(CWnd * pWnd, const CString & strText, CFont * pFont = APE_NULL);
    bool SetScale(double dScale, bool bForce = false);
    Gdiplus::Bitmap * GetMonkeyImage();
    void LoadFont();
    void LoadFont(CFont & Font, int nSizeScale = 100, bool bBold = false);
    void GetMenuColors(COLORREF & rgbMenuBackground, COLORREF & rgbMenuHighlight, COLORREF & rgbMenuText, COLORREF & rgbMenuGrayText, COLORREF & rgbMenuHighlightText, COLORREF & rgbDivider);
    UINT & GetMonitorDPI() { return m_nMonitorDPI; }

    // message map
    DECLARE_MESSAGE_MAP()

private:
    // helper functions
    void DestroyObjects();

    // helper objects
    APE::CSmartPtr<CFormatArray> m_sparyFormats;
    APE::CSmartPtr<CMACSettings> m_spSettings;
    CImageList m_ImageListToolbar;
    CImageList m_ImageListToolbarDisabled;
    CImageList m_ImageListOptionsList;
    CImageList m_ImageListOptionsPages;
    CImageList m_ImageListMenu;
    CGDIBitmapPtr m_spbmpButtons;
    CGDIBitmapPtr m_spbmpMonkey;
    double m_dScale;
    HANDLE m_hSingleInstance;
    bool m_bAnotherInstanceRunning;
    CMACDlg * m_pMACDlg;
    CFont m_Font;
    UINT m_nMonitorDPI;
    HICON m_hCheckIcon;
};

extern CMACApp theApp;
