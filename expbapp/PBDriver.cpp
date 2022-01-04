#include "pch.h"
#include "PBDriver.h"

bool TPBDriver::GetSettings(PROC_BLOCK_SETTINGS& settings) const noexcept {
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, IOCTL_GET_SETTINGS, nullptr, 0, &settings, sizeof(settings), &bytesReturned, nullptr);
}

bool TPBDriver::SetSettings(const PROC_BLOCK_SETTINGS& settings) const noexcept {
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, IOCTL_SET_SETTINGS, nullptr, 0, const_cast<PROC_BLOCK_SETTINGS*>(&settings), sizeof(settings), &bytesReturned, nullptr);
}

bool TPBDriver::GetPaths(IN WCHAR* pPaths, IN DWORD pathBufferSize, OUT DWORD* pBytesReturned, IN DWORD fromEntry /*=0*/) const noexcept {
    DeviceIoControl(m_hDevice, IOCTL_GET_PATHS, &fromEntry, sizeof(fromEntry), pPaths, pathBufferSize, pBytesReturned, nullptr);
    return true;
}

bool TPBDriver::AddPath(const WCHAR* pPath) const noexcept {
    return AddDelPath(true, pPath);
}

bool TPBDriver::DelPath(const WCHAR* pPath) const noexcept {
    return AddDelPath(false, pPath);
}

bool TPBDriver::AddDelPath(bool isAdd, const WCHAR* pPath) const noexcept {
    DWORD bytesReturned = 0, dataLength = 0;

    PathBuffer* pathBuffer = GetPathBuffer(isAdd, pPath, &dataLength);
    if (pathBuffer) {
        bool result = DeviceIoControl(m_hDevice, IOCTL_SET_PATH, nullptr, 0, pathBuffer, dataLength, &bytesReturned, nullptr);
        delete pathBuffer;
        return result;
    }
    else
        return false;
}

// Need to free PathBuffer* manualy
PathBuffer* TPBDriver::GetPathBuffer(IN bool IsAdd, IN const WCHAR* pPath, OUT DWORD* dataLength) const noexcept {
    DWORD pathSize = (DWORD)((wcslen(pPath) + 1) * sizeof(WCHAR));
    *dataLength = (DWORD)(sizeof(PathBuffer) - sizeof(PathBuffer::buffer) + pathSize);
    PathBuffer* pathBuffer = (PathBuffer *) new (std::nothrow) CHAR[*dataLength];

    if (pathBuffer) {
        pathBuffer->add = IsAdd;
        memcpy(pathBuffer->buffer, pPath, pathSize);
    }

    return pathBuffer;
}
