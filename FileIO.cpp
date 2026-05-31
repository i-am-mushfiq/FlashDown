#include "FileIO.h"

static const DWORD kWarnSizeBytes = 2 * 1024 * 1024; // 2 MB

bool FileIO::Read(const std::wstring& path, std::string& outContent)
{
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr,
            (L"Cannot open file:\n" + path).c_str(),
            L"FlashDown",
            MB_ICONERROR | MB_OK
        );
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        MessageBoxW(nullptr, L"Cannot determine file size.", L"FlashDown", MB_ICONERROR | MB_OK);
        return false;
    }

    if (fileSize.QuadPart > kWarnSizeBytes) {
        int result = MessageBoxW(nullptr,
            L"This file is larger than 2 MB. Opening it may be slow.\n\nContinue?",
            L"FlashDown",
            MB_ICONWARNING | MB_OKCANCEL
        );
        if (result == IDCANCEL) {
            CloseHandle(hFile);
            return false;
        }
    }

    DWORD size = static_cast<DWORD>(fileSize.QuadPart);
    outContent.resize(size);

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &outContent[0], size, &bytesRead, nullptr) || bytesRead != size) {
        CloseHandle(hFile);
        MessageBoxW(nullptr, L"Failed to read file.", L"FlashDown", MB_ICONERROR | MB_OK);
        outContent.clear();
        return false;
    }
    CloseHandle(hFile);

    // Strip UTF-8 BOM (EF BB BF)
    if (outContent.size() >= 3 &&
        static_cast<unsigned char>(outContent[0]) == 0xEF &&
        static_cast<unsigned char>(outContent[1]) == 0xBB &&
        static_cast<unsigned char>(outContent[2]) == 0xBF)
    {
        outContent.erase(0, 3);
    }

    return true;
}

bool FileIO::Write(const std::wstring& path, const std::string& content)
{
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr,
            (L"Cannot write file:\n" + path).c_str(),
            L"FlashDown",
            MB_ICONERROR | MB_OK
        );
        return false;
    }

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()), &bytesWritten, nullptr);
    CloseHandle(hFile);

    if (!ok || bytesWritten != static_cast<DWORD>(content.size())) {
        MessageBoxW(nullptr, L"Failed to write file.", L"FlashDown", MB_ICONERROR | MB_OK);
        return false;
    }

    return true;
}
