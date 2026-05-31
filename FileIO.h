#pragma once
#include <windows.h>
#include <string>

namespace FileIO {
    // Read file at path into outContent (UTF-8, BOM stripped).
    // Shows warning MessageBox for files > 2 MB (user can cancel load).
    // Shows error MessageBox and returns false on failure.
    bool Read(const std::wstring& path, std::string& outContent);

    // Overwrite file at path with UTF-8 content.
    // Shows error MessageBox and returns false on failure.
    bool Write(const std::wstring& path, const std::string& content);
}
