#include "ToolbarWindow.h"
#include "Resource.h"
#include <new>
#include <uxtheme.h>
#include <vssym32.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

// Toolbar colours
static const COLORREF kBgColor   = RGB(0x19, 0x19, 0x19);
static const COLORREF kTextColor  = RGB(0xE0, 0xE0, 0xE0);
static const COLORREF kHoverColor = RGB(0x2A, 0x2A, 0x2A);
static const COLORREF kPressColor = RGB(0x35, 0x35, 0x35);

static const int kBtnPadX   = 8;   // left margin
static const int kBtnSpacing = 4;   // gap between buttons
static const int kBtnW       = 80;
static const int kBtnH       = 22;

struct ButtonState {
    RECT  rc;
    bool  hover;
    bool  pressed;
    bool  visible;
    int   id;
    const wchar_t* label;
};

// Per-window data hung off GWLP_USERDATA
struct ToolbarData {
    HBRUSH  hbrBg;
    HBRUSH  hbrHover;
    HBRUSH  hbrPress;
    bool    editMode;
    bool    tracking;  // mouse tracking active
    ButtonState btns[3]; // Edit, Save, Preview
};

static void UpdateButtonLayout(ToolbarData* td, int tbWidth)
{
    int y = (TOOLBAR_HEIGHT - kBtnH) / 2;
    int x = kBtnPadX;

    for (auto& b : td->btns) {
        b.rc = { x, y, x + kBtnW, y + kBtnH };
        x += kBtnW + kBtnSpacing;
    }
    (void)tbWidth;

    // Visibility driven by mode
    td->btns[0].visible = !td->editMode;  // Edit
    td->btns[1].visible =  td->editMode;  // Save
    td->btns[2].visible =  td->editMode;  // Preview
}

static void DrawButton(HDC hdc, ButtonState& b, HBRUSH hbrHover, HBRUSH hbrPress)
{
    if (!b.visible) return;

    // Background
    HBRUSH hbr = b.pressed ? hbrPress : (b.hover ? hbrHover : nullptr);
    if (hbr)
        FillRect(hdc, &b.rc, hbr);

    // Label
    SetTextColor(hdc, kTextColor);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, b.label, -1, &b.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ToolbarData* td = reinterpret_cast<ToolbarData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CREATE:
    {
        td = new(std::nothrow) ToolbarData{};
        if (!td) return -1;
        td->hbrBg    = CreateSolidBrush(kBgColor);
        td->hbrHover = CreateSolidBrush(kHoverColor);
        td->hbrPress = CreateSolidBrush(kPressColor);
        td->editMode = false;
        td->tracking = false;

        td->btns[0] = { {}, false, false, true,  IDC_BTN_EDIT,    L"Edit" };
        td->btns[1] = { {}, false, false, false, IDC_BTN_SAVE,    L"Save" };
        td->btns[2] = { {}, false, false, false, IDC_BTN_PREVIEW, L"Preview" };

        RECT rc; GetClientRect(hwnd, &rc);
        UpdateButtonLayout(td, rc.right);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(td));
        return 0;
    }

    case WM_DESTROY:
        if (td) {
            DeleteObject(td->hbrBg);
            DeleteObject(td->hbrHover);
            DeleteObject(td->hbrPress);
            delete td;
        }
        return 0;

    case WM_ERASEBKGND:
    {
        if (!td) break;
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, td->hbrBg);
        return 1;
    }

    case WM_PAINT:
    {
        if (!td) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, td->hbrBg);

        // Top separator line
        HPEN hpen = CreatePen(PS_SOLID, 1, RGB(0x33, 0x33, 0x33));
        HPEN hOld = static_cast<HPEN>(SelectObject(hdc, hpen));
        MoveToEx(hdc, 0, 0, nullptr);
        LineTo(hdc, rc.right, 0);
        SelectObject(hdc, hOld);
        DeleteObject(hpen);

        HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));

        for (auto& b : td->btns)
            DrawButton(hdc, b, td->hbrHover, td->hbrPress);

        SelectObject(hdc, hOldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!td) break;
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        bool changed = false;
        for (auto& b : td->btns) {
            if (!b.visible) continue;
            bool over = (mx >= b.rc.left && mx < b.rc.right &&
                         my >= b.rc.top  && my < b.rc.bottom);
            if (over != b.hover) { b.hover = over; changed = true; }
        }
        if (!td->tracking) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            td->tracking = true;
        }
        if (changed) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (td) {
            for (auto& b : td->btns) b.hover = false;
            td->tracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        if (!td) break;
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        for (auto& b : td->btns) {
            if (!b.visible) continue;
            if (mx >= b.rc.left && mx < b.rc.right &&
                my >= b.rc.top  && my < b.rc.bottom)
            {
                b.pressed = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!td) break;
        ReleaseCapture();
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        for (auto& b : td->btns) {
            if (!b.visible) continue;
            if (b.pressed) {
                b.pressed = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                // Fire command if released inside button
                if (mx >= b.rc.left && mx < b.rc.right &&
                    my >= b.rc.top  && my < b.rc.bottom)
                {
                    SendMessageW(GetParent(hwnd), WM_COMMAND,
                                 MAKEWPARAM(b.id, BN_CLICKED),
                                 reinterpret_cast<LPARAM>(hwnd));
                }
                break;
            }
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (!td) break;
        UpdateButtonLayout(td, LOWORD(lParam));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ToolbarWindow::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ToolbarProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FlashDown_Toolbar";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    return RegisterClassExW(&wc) != 0;
}

HWND ToolbarWindow::Create(HWND hParent, HINSTANCE hInst, int parentWidth)
{
    RECT parentRect;
    GetClientRect(hParent, &parentRect);
    int parentH = parentRect.bottom - parentRect.top;

    return CreateWindowW(
        L"FlashDown_Toolbar",
        nullptr,
        WS_CHILD | WS_VISIBLE,
        0, parentH - TOOLBAR_HEIGHT,
        parentWidth, TOOLBAR_HEIGHT,
        hParent, nullptr, hInst, nullptr
    );
}

void ToolbarWindow::SetMode(HWND hToolbar, bool editMode)
{
    ToolbarData* td = reinterpret_cast<ToolbarData*>(
        GetWindowLongPtrW(hToolbar, GWLP_USERDATA));
    if (!td) return;
    td->editMode = editMode;

    RECT rc; GetClientRect(hToolbar, &rc);
    UpdateButtonLayout(td, rc.right);
    InvalidateRect(hToolbar, nullptr, TRUE);
}
