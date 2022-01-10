#pragma once

#include <string>

std::wstring GetLastErrorStr();

void ShowErrorMsg(HWND hwnd, const WCHAR* pText, const WCHAR* pCaption, LONG msgBoxType);
