#include "pch.h"
#include "PBDriver.h"

bool TPBDriver::GetSettings(PROC_BLOCK_SETTINGS& settings) const noexcept {
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, IOCTL_GET_SETTINGS, nullptr, 0, &settings, sizeof(settings), &bytesReturned, nullptr);
}

bool TPBDriver::SetSettings(const PROC_BLOCK_SETTINGS& settings) const noexcept {
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, IOCTL_SET_SETTINGS, const_cast<PROC_BLOCK_SETTINGS*>(&settings), sizeof(settings), nullptr, 0, &bytesReturned, nullptr);
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
    DWORD pathSize = (DWORD)((wcslen(pPath) + 1) * sizeof(WCHAR));
    DWORD dataLength = (DWORD)(sizeof(PathBuffer) - sizeof(PathBuffer::buffer) + pathSize);
    PathBuffer* pathBuffer = (PathBuffer*) new (std::nothrow) CHAR[dataLength];

    if (pathBuffer) {
        DWORD bytesReturned = 0;

        pathBuffer->add = isAdd;
        memcpy(pathBuffer->buffer, pPath, pathSize);

        bool result = DeviceIoControl(m_hDevice, IOCTL_SET_PATH, pathBuffer, dataLength, nullptr, 0, &bytesReturned, nullptr);
        operator delete(pathBuffer, dataLength);
        return result;
    }
    else
        return false;
}

bool TPBDriver::DelAllPaths() const noexcept {
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, IOCTL_CLEAR_PATHS, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
}
