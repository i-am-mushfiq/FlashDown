// ATL must be first so COM infrastructure is established before any COM headers
#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>

#include "MainWindow.h"
#include "Resource.h"
#include "BrowserHost.h"
#include "ToolbarWindow.h"
#include "SplitterWindow.h"
#include "EditModeController.h"
#include "MarkdownPipeline.h"
#include "FileIO.h"

#include <windows.h>
#include <string>

// ---------------------------------------------------------------------------
// Globals — shared across modules
// ---------------------------------------------------------------------------
std::wstring  g_strFilePath;
std::string   g_strMarkdown;
HWND          g_hToolbar = nullptr;

// Cold-start benchmark checkpoint (defined in main.cpp, no-op when disabled)
extern void BenchCheckpoint(const wchar_t* name);

static HINSTANCE s_hInst = nullptr;
static HBRUSH    s_hbrBg = nullptr;  // main window background

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void UpdateTitle(HWND hwnd)
{
    // Extract filename component only
    std::wstring name = g_strFilePath;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name = name.substr(slash + 1);
    SetWindowTextW(hwnd, (name + L" - FlashDown").c_str());
}

// Returns client rect minus toolbar
static RECT ContentRect(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.bottom -= TOOLBAR_HEIGHT;
    if (rc.bottom < 0) rc.bottom = 0;
    return rc;
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // ------------------------------------------------------------------
    case WM_CREATE:
    {
        s_hbrBg = CreateSolidBrush(RGB(0x19, 0x19, 0x19));

        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        g_hToolbar = ToolbarWindow::Create(hwnd, s_hInst, w);

        // The WebBrowser ActiveX control is NOT created here.
        // Creating AtlAxWin inside WM_CREATE causes the WebBrowser COM object
        // to call back into a partially-initialised parent window, which
        // results in STATUS_FATAL_USER_CALLBACK_EXCEPTION (0xC000041D).
        // BrowserHost::Create is called in WM_APP_LOADFILE instead, after
        // WM_CREATE has returned and the parent window is fully initialised.

        PostMessageW(hwnd, WM_APP_LOADFILE, 0, 0);
        return 0;
    }

    // ------------------------------------------------------------------
    case WM_APP_LOADFILE:
    {
        BenchCheckpoint(L"WM_APP_LOADFILE start");

        // Set up the dark blank page (async via event sink if doc not ready).
        BrowserHost::LoadBlankDark();
        BenchCheckpoint(L"After LoadBlankDark");

        if (g_strFilePath.empty()) return 0;

        std::string content;
        if (!FileIO::Read(g_strFilePath, content))
        {
            PostQuitMessage(0);
            return 0;
        }
        g_strMarkdown = content;
        UpdateTitle(hwnd);
        BenchCheckpoint(L"After FileIO::Read");

        std::wstring html = MarkdownPipeline::Convert(g_strMarkdown);
        BenchCheckpoint(L"After MarkdownPipeline::Convert");

        if (!html.empty())
            BrowserHost::NavigateTo(html);
        BenchCheckpoint(L"After BrowserHost::NavigateTo (HTML delivered)");

        return 0;
    }

    // ------------------------------------------------------------------
    case WM_APP_SPLITMOVE:
    {
        int pct = static_cast<int>(wParam);
        EditModeController::SetSplitPct(pct);
        RECT cr = ContentRect(hwnd);
        EditModeController::ResizePanes(cr.right, cr.bottom, pct);
        return 0;
    }

    // ------------------------------------------------------------------
    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        int contentH = h - TOOLBAR_HEIGHT;
        if (contentH < 0) contentH = 0;

        if (g_hToolbar)
            MoveWindow(g_hToolbar, 0, contentH, w, TOOLBAR_HEIGHT, TRUE);

        // Browser may not exist yet if WM_APP_LOADFILE hasn't fired
        if (!BrowserHost::IsCreated()) return 0;

        if (EditModeController::IsActive())
        {
            EditModeController::ResizePanes(w, contentH,
                                             EditModeController::GetSplitPct());
        }
        else
        {
            RECT br = { 0, 0, w, contentH };
            BrowserHost::Reposition(br);
        }
        return 0;
    }

    // ------------------------------------------------------------------
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 400;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }

    // ------------------------------------------------------------------
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        RECT cr = ContentRect(hwnd);

        switch (id)
        {
        case IDC_BTN_EDIT:
            EditModeController::Enter(hwnd, s_hInst, cr.right, cr.bottom);
            break;

        case IDC_BTN_SAVE:
            EditModeController::Save();
            break;

        case IDC_BTN_PREVIEW:
            EditModeController::Exit(hwnd, cr.right, cr.bottom);
            break;
        }
        return 0;
    }

    // ------------------------------------------------------------------
    // Dark-theme the edit control
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(0xE0, 0xE0, 0xE0));
        SetBkColor(hdc, RGB(0x19, 0x19, 0x19));
        HBRUSH hbr = EditModeController_GetEditBrush();
        return hbr ? reinterpret_cast<LRESULT>(hbr)
                   : reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    }

    // ------------------------------------------------------------------
    // Suppress right-click context menu in browser pane
    case WM_CONTEXTMENU:
        return 0;

    // ------------------------------------------------------------------
    // Focus management for the embedded Trident control (#13/#14).
    // The OS routes wheel and keyboard input based on focus; without
    // these handlers, focus stays on FlashDown_Main and the IE Server
    // never receives scroll events. The diagnostic (Ctrl+F12 build)
    // confirmed: SetFocus on the IE Server takes successfully and
    // scroll input is processed once it has focus.
    case WM_SETFOCUS:
        if (!EditModeController::IsActive())
            BrowserHost::FocusBrowser();
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && !EditModeController::IsActive())
            BrowserHost::FocusBrowser();
        break;

    // ------------------------------------------------------------------
    case WM_ERASEBKGND:
    {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, s_hbrBg);
        return 1;
    }

    // ------------------------------------------------------------------
    case WM_DESTROY:
        BrowserHost::Release();
        if (s_hbrBg) { DeleteObject(s_hbrBg); s_hbrBg = nullptr; }
        PostQuitMessage(0);
        return 0;

    // ------------------------------------------------------------------
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
int MainWindow::ContentHeight(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int h = rc.bottom - rc.top - TOOLBAR_HEIGHT;
    return h > 0 ? h : 0;
}

bool MainWindow::RegisterClass(HINSTANCE hInst)
{
    s_hInst = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FlashDown_Main";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    // Background is painted in WM_ERASEBKGND; no class brush needed.
    return RegisterClassExW(&wc) != 0;
}

HWND MainWindow::Create(HINSTANCE hInst)
{
    return CreateWindowExW(
        0,
        L"FlashDown_Main",
        L"FlashDown",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        900, 700,
        nullptr, nullptr, hInst, nullptr
    );
}
