#include "SplitterWindow.h"
#include "Resource.h"
#include <new>

static const COLORREF kSplitterColor = RGB(0x2A, 0x2A, 0x2A);
static const int kSplitterWidth = 4;

struct SplitterData {
    bool dragging;
    int  startX;
    HBRUSH hbr;
};

static LRESULT CALLBACK SplitterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SplitterData* sd = reinterpret_cast<SplitterData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CREATE:
    {
        sd = new(std::nothrow) SplitterData{};
        if (!sd) return -1;
        sd->dragging = false;
        sd->startX   = 0;
        sd->hbr      = CreateSolidBrush(kSplitterColor);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(sd));
        return 0;
    }

    case WM_DESTROY:
        if (sd) { DeleteObject(sd->hbr); delete sd; }
        return 0;

    case WM_ERASEBKGND:
    {
        if (!sd) break;
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, sd->hbr);
        return 1;
    }

    case WM_PAINT:
    {
        if (!sd) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, sd->hbr);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        return TRUE;

    case WM_LBUTTONDOWN:
    {
        if (!sd) break;
        sd->dragging = true;
        SetCapture(hwnd);
        // Convert current mouse X to parent-client coordinates
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ClientToScreen(hwnd, &pt);
        HWND hParent = GetParent(hwnd);
        ScreenToClient(hParent, &pt);
        sd->startX = pt.x;
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!sd || !sd->dragging) break;
        // Convert mouse X to parent-client space
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ClientToScreen(hwnd, &pt);
        HWND hParent = GetParent(hwnd);
        ScreenToClient(hParent, &pt);

        RECT parentRc; GetClientRect(hParent, &parentRc);
        int clientW = parentRc.right - parentRc.left;
        if (clientW <= 0) break;

        int pct = (pt.x * 100) / clientW;
        if (pct < 20) pct = 20;
        if (pct > 80) pct = 80;

        PostMessageW(hParent, WM_APP_SPLITMOVE, static_cast<WPARAM>(pct), 0);
        return 0;
    }

    case WM_LBUTTONUP:
        if (sd) { sd->dragging = false; ReleaseCapture(); }
        return 0;

    case WM_CAPTURECHANGED:
        if (sd) sd->dragging = false;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool SplitterWindow::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SplitterProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FlashDown_Splitter";
    wc.hCursor       = LoadCursor(nullptr, IDC_SIZEWE);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    return RegisterClassExW(&wc) != 0;
}

HWND SplitterWindow::Create(HWND hParent, HINSTANCE hInst, int x, int y, int height)
{
    return CreateWindowW(
        L"FlashDown_Splitter",
        nullptr,
        WS_CHILD | WS_VISIBLE,
        x, y, kSplitterWidth, height,
        hParent, nullptr, hInst, nullptr
    );
}
