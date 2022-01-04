#include "pch.h"
#include "PBDriver.h"

bool TPBDriver::GetSettings(PROC_BLOCK_SETTINGS& settings) const noexcept {
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, IOCTL_GET_SETTINGS, nullptr, 0, &settings, sizeof(settings), &bytesReturned, nullptr);
}

bool TPBDriver::GetPaths(IN WCHAR* paths, IN const DWORD pathBufferSize, OUT DWORD* pBytesReturned, IN DWORD fromEntry /*=0*/) const noexcept {
    DeviceIoControl(m_hDevice, IOCTL_GET_PATHS, &fromEntry, sizeof(fromEntry), paths, pathBufferSize, pBytesReturned, nullptr);
    int i = GetLastError();
    return true;
}

bool TPBDriver::AddPath(const WCHAR* path) const noexcept {
    DWORD bytesReturned = 0, dataLength = 0;
    
    PathBuffer* pathBuffer = GetPathBuffer(true, path, &dataLength);
    if (pathBuffer) {
        bool result = DeviceIoControl(m_hDevice, IOCTL_ADD_PATH, nullptr, 0, pathBuffer, dataLength, &bytesReturned, nullptr);
        delete pathBuffer;
        return result;
    }
    else
        return false;
}

// Need to free PathBuffer* manualy
PathBuffer* TPBDriver::GetPathBuffer(IN const bool IsAdd, IN const WCHAR* path, OUT DWORD* dataLength) const noexcept {
    DWORD pathSize = (DWORD)((wcslen(path) + 1) * sizeof(WCHAR));
    *dataLength = (DWORD)(sizeof(PathBuffer) - sizeof(PathBuffer::buffer) + pathSize);
    PathBuffer* pathBuffer = (PathBuffer *) new (std::nothrow) CHAR[*dataLength];

    if (pathBuffer) {
        pathBuffer->add = IsAdd;
        memcpy(pathBuffer->buffer, path, pathSize);
    }

    return pathBuffer;
}
