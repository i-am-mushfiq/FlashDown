#pragma once
#include <windows.h>

namespace MainWindow {
    bool RegisterClass(HINSTANCE hInst);
    HWND Create(HINSTANCE hInst);
    // Returns the content area height (client height minus toolbar)
    int  ContentHeight(HWND hwnd);
}

// Declared here so EditModeController.cpp can call it.
// Returns the dark brush for WM_CTLCOLOREDIT.
HBRUSH EditModeController_GetEditBrush();
