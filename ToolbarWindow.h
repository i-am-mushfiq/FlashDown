#pragma once
#include <windows.h>

#define TOOLBAR_HEIGHT 28  // device pixels (PRD: 28 DIP)

namespace ToolbarWindow {
    // Registers the toolbar window class. Call once in WinMain before CreateWindow.
    bool RegisterClass(HINSTANCE hInst);

    // Creates the toolbar child window docked to the bottom of the parent.
    HWND Create(HWND hParent, HINSTANCE hInst, int parentWidth);

    // Switch button state: false = preview mode (Edit button visible),
    //                      true  = edit mode (Save + Preview buttons visible).
    void SetMode(HWND hToolbar, bool editMode);
}
