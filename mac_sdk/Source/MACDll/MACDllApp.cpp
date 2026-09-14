#include "stdafx.h"
#include "MACDllApp.h"
#include "DirectShowSDK/streams.h"

CMACDllApp g_Application;

BEGIN_MESSAGE_MAP(CMACDllApp, CWinApp)
END_MESSAGE_MAP()

CMACDllApp::CMACDllApp() noexcept
{
}

CMACDllApp::~CMACDllApp()
{
}

BOOL CMACDllApp::InitInstance()
{
    // store the instances for the DirectShow filter
    g_hInst = AfxGetInstanceHandle();

    // parent
    return CWinApp::InitInstance();
}
