#pragma once
#include <windows.h>

namespace SplitterWindow {
    // Registers the splitter window class. Call once before CreateWindow.
    bool RegisterClass(HINSTANCE hInst);

    // Creates the 4-pixel splitter bar.
    // hParent must handle WM_APP_SPLITMOVE: wParam = new split percentage [20,80].
    HWND Create(HWND hParent, HINSTANCE hInst, int x, int y, int height);
}
