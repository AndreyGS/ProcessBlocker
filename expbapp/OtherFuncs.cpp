#include "pch.h"
#include "OtherFuncs.h"

std::wstring GetLastErrorStr() {
    DWORD i = GetLastError();

    if (i == 0)
        return std::wstring();

    LPWSTR pStr = nullptr;

    size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS
        , nullptr, i, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&pStr, 0, nullptr);

    std::wstring msg(pStr, size);
    LocalFree(pStr);

    return msg;
}

void ShowErrorMsg(HWND hwnd, const WCHAR* pText, const WCHAR* pCaption, LONG msgBoxType) {
    std::wstring errorMsg(pText);
    errorMsg += GetLastErrorStr();
    MessageBoxW(hwnd, errorMsg.c_str(), L"", MB_ICONERROR);
}
