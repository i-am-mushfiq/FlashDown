#pragma once
#include <windows.h>
#include <string>

namespace BrowserHost {
    // Creates the AtlAxWin child window and acquires IWebBrowser2.
    // Call LoadBlankDark() immediately after to prevent white flash.
    // Returns NULL on failure.
    HWND Create(HWND hParent, HINSTANCE hInst, RECT rect);

    // Loads minimal blank dark page to prevent white flash before content arrives.
    void LoadBlankDark();

    // Navigates the browser to the given complete HTML document (UTF-16).
    void NavigateTo(const std::wstring& html);

    // Moves/resizes the browser child window without reloading content.
    void Reposition(RECT rect);

    // Returns true if the browser window has been created.
    bool IsCreated();

    // Returns the container HWND (AtlAxWin host), or nullptr.
    HWND GetHWND();

    // Walks the child tree to find the inner "Internet Explorer_Server"
    // window (where Trident's actual document lives). Returns nullptr
    // if not yet present.
    HWND FindServerHWND();

    // Sets keyboard focus to the IE server so it receives wheel/scroll keys.
    void FocusBrowser();

    // Releases COM interfaces. Call before DestroyWindow on the container.
    void Release();
}
