// FlashDown — single-file Markdown viewer for Windows
// Entry point: WinMain

// ATL headers must come before any raw COM / exdisp headers so that
// atlbase.h establishes the COM infrastructure (objbase.h, etc.) first.
#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>    // AtlAxWinInit

// CAtlExeModuleT sets _pAtlModule, which ATL COM objects (CComPolyObject<CAxHostWindow>
// etc.) call during construction via _pAtlModule->Lock(). Without this global,
// _pAtlModule is NULL and CreateWindowW("AtlAxWin140") causes an access violation.
struct CFlashDownModule : public ATL::CAtlExeModuleT<CFlashDownModule> {};
CFlashDownModule _AtlModule;

#include <windows.h>
#include <string>

#include "MainWindow.h"
#include "BrowserHost.h"
#include "ToolbarWindow.h"
#include "SplitterWindow.h"
#include "Resource.h"

// Defined in MainWindow.cpp; accessed by EditModeController.cpp
extern std::wstring g_strFilePath;

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

// ---------------------------------------------------------------------------
// Full cold-start benchmark — set to 1 to trace every stage from wWinMain
// entry to first rendered frame.  Output lands in DebugView (SysInternals).
//   [Bench] Stage name                      | +delta ms | total:  X.XXX ms
// When set to 0 the instrumentation compiles to zero code.
// ---------------------------------------------------------------------------
#define FULL_BENCHMARK 0
#if FULL_BENCHMARK
#include <stdio.h>
static LARGE_INTEGER g_benchFreq, g_benchT0, g_benchPrev;
#endif

void BenchCheckpoint(const wchar_t* name)
{
#if FULL_BENCHMARK
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double total = (double)(now.QuadPart - g_benchT0.QuadPart)
                 * 1000.0 / (double)g_benchFreq.QuadPart;
    double delta = (double)(now.QuadPart - g_benchPrev.QuadPart)
                 * 1000.0 / (double)g_benchFreq.QuadPart;
    wchar_t buf[256];
    swprintf_s(buf, L"[Bench] %-40s | +%7.3f ms | total: %7.3f ms",
               name, delta, total);
    OutputDebugStringW(buf);
    g_benchPrev = now;
#endif
}

#if FULL_BENCHMARK
#define BENCH_INIT() do { \
    QueryPerformanceFrequency(&g_benchFreq); \
    QueryPerformanceCounter(&g_benchT0); \
    g_benchPrev = g_benchT0; \
    BenchCheckpoint(L"wWinMain entry"); \
} while(0)
#else
#define BENCH_INIT() ((void)0)
#endif

// ---------------------------------------------------------------------------
// Parse the first argument from the command line (handles quoted paths)
// ---------------------------------------------------------------------------
static std::wstring ParseFilePath(LPWSTR lpCmdLine)
{
    if (!lpCmdLine || !*lpCmdLine) return {};

    std::wstring arg(lpCmdLine);

    size_t start = arg.find_first_not_of(L" \t");
    if (start == std::wstring::npos) return {};
    arg = arg.substr(start);

    if (arg.front() == L'"')
    {
        size_t end = arg.find(L'"', 1);
        if (end == std::wstring::npos) end = arg.size();
        return arg.substr(1, end - 1);
    }
    else
    {
        size_t end = arg.find_first_of(L" \t");
        return arg.substr(0, end);
    }
}

// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int)
{
    BENCH_INIT();

    // DPI awareness (#15) — System-DPI-aware, matching the manifest.
    // Trident jitter under PerMonitorV2 is caused by layout-in-DIPs +
    // raster-on-primary-grid + bitmap-rescale; pinning to a single DPI
    // for the process lifetime stabilises text metrics.
    // Win8.1+ has SetProcessDpiAwareness; older systems fall back silently.
    typedef HRESULT (WINAPI *PFN_SetPDA)(int /*PROCESS_DPI_AWARENESS*/);
    if (HMODULE hShc = LoadLibraryW(L"Shcore.dll"))
    {
        if (auto fn = reinterpret_cast<PFN_SetPDA>(
                GetProcAddress(hShc, "SetProcessDpiAwareness")))
            fn(1 /*PROCESS_SYSTEM_DPI_AWARE*/);
    }
    BenchCheckpoint(L"After DPI awareness");

    // Force IE11 Standards mode for the embedded Trident control (#15).
    // 12001 = "always IE11 Standards regardless of doctype" — more
    // stable than 11000 (which honours the page doctype and can fall
    // through to a quirks-mode raster path on edge-case markup).
    {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Internet Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            DWORD ie11Std = 12001;
            RegSetValueExW(hKey, L"FlashDown.exe", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&ie11Std), sizeof(ie11Std));
            RegCloseKey(hKey);
        }
    }

    g_strFilePath = ParseFilePath(lpCmdLine);
    BenchCheckpoint(L"After IE emulation + cmdline parse");

    // OLE must be initialised before AtlAxWin creates the WebBrowser ActiveX
    // control (CoCreateInstance requires a COM apartment).
    OleInitialize(nullptr);
    BenchCheckpoint(L"After OleInitialize");

    // Register the AtlAxWin window class (houses the WebBrowser control)
    AtlAxWinInit();
    BenchCheckpoint(L"After AtlAxWinInit");

    if (!MainWindow::RegisterClass(hInst))    return 1;
    if (!ToolbarWindow::RegisterClass(hInst)) return 1;
    if (!SplitterWindow::RegisterClass(hInst)) return 1;
    BenchCheckpoint(L"After window class registration");

    HWND hwnd = MainWindow::Create(hInst);
    if (!hwnd) return 1;
    BenchCheckpoint(L"After MainWindow::Create (WM_CREATE done)");

    // Open maximized so the preview fills the screen on launch (#16).
    // The restore state still respects the 900x700 / 400x300 min from
    // FR6, so users who un-maximize fall back to a sensible window.
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);
    BenchCheckpoint(L"After ShowWindow + UpdateWindow");

    // Create the browser control HERE — in wWinMain, not inside any window
    // message handler. Creating AtlAxWin inside DispatchMessage causes COM's
    // STA machinery to attempt re-entrant message delivery, which crashes.
    {
        RECT rc; GetClientRect(hwnd, &rc);
        int contentH = MainWindow::ContentHeight(hwnd);
        RECT browserRc = { 0, 0, rc.right - rc.left, contentH };
        BrowserHost::Create(hwnd, hInst, browserRc);
        // LoadBlankDark is called from WM_APP_LOADFILE in the message loop.
    }
    BenchCheckpoint(L"After BrowserHost::Create + PostMessage");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
