#pragma once

#include "..\exprocblock\exproccommon.h"

class TPBDriver {
    HANDLE m_hDevice;

public:
    TPBDriver() : m_hDevice(INVALID_HANDLE_VALUE) { 
        m_hDevice = CreateFile(L"\\\\.\\ProcsBlocker", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, NULL); 
    }

    ~TPBDriver() { if (IsValid()) CloseHandle(m_hDevice); }

    bool IsValid() const noexcept { return m_hDevice != INVALID_HANDLE_VALUE; }

    bool GetSettings(PROC_BLOCK_SETTINGS& settings) const noexcept;
    bool GetPaths(IN WCHAR* paths,IN const DWORD pathBufferSize, OUT DWORD* pBytesReturned, IN const DWORD fromEntry = 0) const noexcept;

    bool AddPath(const WCHAR* path) const noexcept;

private:
    PathBuffer* GetPathBuffer(IN const bool IsAdd, IN const WCHAR* path, OUT DWORD* dataLength) const noexcept;
};