#pragma once
#include <windows.h>
#include <string>

namespace EditModeController {
    // Enter side-by-side edit mode.
    // clientHeight = height of client area excluding toolbar.
    void Enter(HWND hMainWnd, HINSTANCE hInst, int clientWidth, int clientHeight);

    // Exit edit mode: re-render from current editor content, restore full browser.
    void Exit(HWND hMainWnd, int clientWidth, int clientHeight);

    // Save editor text to disk and refresh preview.
    void Save();

    // Resize edit/splitter/browser panes after WM_SIZE or splitter drag.
    // splitPct = left pane percentage [20,80].
    void ResizePanes(int clientWidth, int clientHeight, int splitPct);

    // Accessors used by MainWindow
    bool IsActive();
    int  GetSplitPct();
    void SetSplitPct(int pct);
}
