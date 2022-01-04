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
    bool SetSettings(const PROC_BLOCK_SETTINGS& settings) const noexcept;
    bool GetPaths(IN WCHAR* pPaths, IN DWORD pathBufferSize, OUT DWORD* pBytesReturned, IN const DWORD fromEntry = 0) const noexcept;

    bool AddPath(const WCHAR* pPath) const noexcept;
    bool DelPath(const WCHAR* pPath) const noexcept;

private:
    PathBuffer* GetPathBuffer(IN bool IsAdd, IN const WCHAR* pPath, OUT DWORD* dataLength) const noexcept;
    bool AddDelPath(bool isAdd, const WCHAR* pPath) const noexcept;
};