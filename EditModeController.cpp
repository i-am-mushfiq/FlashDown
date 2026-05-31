#include "EditModeController.h"
#include "BrowserHost.h"
#include "MarkdownPipeline.h"
#include "FileIO.h"
#include "ToolbarWindow.h"
#include "SplitterWindow.h"

// Globals defined in main.cpp — declared extern here
extern std::wstring  g_strFilePath;
extern std::string   g_strMarkdown;
extern HWND          g_hToolbar;

static const int kSplitterWidth = 4;
static const COLORREF kEditBg   = RGB(0x19, 0x19, 0x19);
static const COLORREF kEditText = RGB(0xE0, 0xE0, 0xE0);

static bool  s_active   = false;
static int   s_splitPct = 45;
static HWND  s_hEdit    = nullptr;
static HWND  s_hSplitter= nullptr;
static HBRUSH s_hbrEdit = nullptr;
static HFONT s_hFont    = nullptr;

static void DestroyEditControls()
{
    if (s_hEdit)     { DestroyWindow(s_hEdit);     s_hEdit     = nullptr; }
    if (s_hSplitter) { DestroyWindow(s_hSplitter); s_hSplitter = nullptr; }
    if (s_hbrEdit)   { DeleteObject(s_hbrEdit);    s_hbrEdit   = nullptr; }
    if (s_hFont)     { DeleteObject(s_hFont);      s_hFont     = nullptr; }
}

static std::string GetEditText()
{
    if (!s_hEdit) return g_strMarkdown;
    int len = GetWindowTextLengthW(s_hEdit);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(s_hEdit, &wide[0], len + 1);
    wide.resize(static_cast<size_t>(len));

    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                      static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                         static_cast<int>(wide.size()),
                         &utf8[0], needed, nullptr, nullptr);
    return utf8;
}

void EditModeController::Enter(HWND hMainWnd, HINSTANCE hInst,
                                int clientWidth, int clientHeight)
{
    if (s_active) return;
    s_active = true;

    // Create dark brush for edit control background
    s_hbrEdit = CreateSolidBrush(kEditBg);

    // Monospace font for editor
    s_hFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
        L"Consolas"
    );

    // Convert cached UTF-8 markdown to UTF-16 for the Edit control
    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                    g_strMarkdown.c_str(),
                                    static_cast<int>(g_strMarkdown.size()),
                                    nullptr, 0);
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                         g_strMarkdown.c_str(),
                         static_cast<int>(g_strMarkdown.size()),
                         &wide[0], wlen);

    int leftW = (clientWidth * s_splitPct / 100) - kSplitterWidth / 2;

    s_hEdit = CreateWindowExW(
        0,
        L"EDIT",
        wide.c_str(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
        0, 0, leftW, clientHeight,
        hMainWnd, nullptr, hInst, nullptr
    );

    if (s_hFont)
        SendMessageW(s_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(s_hFont), TRUE);

    int splitterX = leftW;
    int rightX    = splitterX + kSplitterWidth;
    int rightW    = clientWidth - rightX;

    s_hSplitter = SplitterWindow::Create(hMainWnd, hInst,
                                          splitterX, 0, clientHeight);

    RECT browserRc = { rightX, 0, rightX + rightW, clientHeight };
    BrowserHost::Reposition(browserRc);

    ToolbarWindow::SetMode(g_hToolbar, true);
    SetFocus(s_hEdit);
}

void EditModeController::Exit(HWND hMainWnd, int clientWidth, int clientHeight)
{
    if (!s_active) return;

    // Capture text BEFORE destroying the Edit control
    std::string currentText = GetEditText();
    g_strMarkdown = currentText;

    DestroyEditControls();
    s_active = false;

    RECT browserRc = { 0, 0, clientWidth, clientHeight };
    BrowserHost::Reposition(browserRc);
    ToolbarWindow::SetMode(g_hToolbar, false);

    // Re-render from the (possibly modified) editor content
    std::wstring html = MarkdownPipeline::Convert(g_strMarkdown);
    if (!html.empty())
        BrowserHost::NavigateTo(html);
}

void EditModeController::Save()
{
    if (!s_active) return;

    std::string text = GetEditText();
    if (FileIO::Write(g_strFilePath, text))
    {
        g_strMarkdown = text;
        std::wstring html = MarkdownPipeline::Convert(g_strMarkdown);
        if (!html.empty())
            BrowserHost::NavigateTo(html);
    }
}

void EditModeController::ResizePanes(int clientWidth, int clientHeight, int splitPct)
{
    s_splitPct = splitPct;
    if (!s_active) return;

    int leftW     = (clientWidth * splitPct / 100) - kSplitterWidth / 2;
    int splitterX = leftW;
    int rightX    = splitterX + kSplitterWidth;
    int rightW    = clientWidth - rightX;

    if (s_hEdit)
        MoveWindow(s_hEdit, 0, 0, leftW, clientHeight, TRUE);

    if (s_hSplitter)
        MoveWindow(s_hSplitter, splitterX, 0, kSplitterWidth, clientHeight, TRUE);

    RECT browserRc = { rightX, 0, rightX + rightW, clientHeight };
    BrowserHost::Reposition(browserRc);
}

bool EditModeController::IsActive()   { return s_active;   }
int  EditModeController::GetSplitPct(){ return s_splitPct; }
void EditModeController::SetSplitPct(int pct)
{
    s_splitPct = pct;
}

// Called from MainWindow WM_CTLCOLOREDIT to apply dark theme to the edit control
// Expose the brush so MainWindow can return it
HBRUSH EditModeController_GetEditBrush() { return s_hbrEdit; }
