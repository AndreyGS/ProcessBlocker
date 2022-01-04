#include "pch.h"
#include "exproccommon.h"

#define REMEMBER_BAD_STATUS_IF_NEED(expr) { \
    NTSTATUS status2 = expr; \
    if (!NT_SUCCESS(status2) && NT_SUCCESS(status)) \
        status = status2; \
}

#define DRIVER_PREFIX "ProcsBlocker: "

constexpr UNICODE_STRING DEVICE_NAME = RTL_CONSTANT_STRING(L"\\Device\\ProcsBlocker");
constexpr UNICODE_STRING SYMBOLIC_LINK = RTL_CONSTANT_STRING(L"\\DosDevices\\ProcsBlocker");

constexpr UNICODE_STRING REGISTRY_BLOCKED_PATHS = RTL_CONSTANT_STRING(L"BlockedPaths");
constexpr UNICODE_STRING REGISTRY_PATHS_MAX_SIZE = RTL_CONSTANT_STRING(L"PathsMaxSize");
constexpr UNICODE_STRING REGISTRY_BLOCKING_ENABLED = RTL_CONSTANT_STRING(L"BlockingEnabled");

constexpr WCHAR END_OF_REG_MULTI_SZ[] = L"";

constexpr ULONG DEFAULT_MAX_DATA_SIZE = PAGE_SIZE << 1;
constexpr UCHAR MIN_DATA_SIZE = sizeof(WCHAR) + sizeof(END_OF_REG_MULTI_SZ);
constexpr ULONG PROCBLOCK_POOL_TAG = 'lbcp';

enum class RegistryValues {
    Paths,
    DataMaxSize,
    BlockingEnabled
};

PROC_BLOCK_SETTINGS g_settings = { 0 };

typedef struct {
    LIST_ENTRY Entry;
    UNICODE_STRING path;
} BLOCKED_PROCESS_PATH, * PBLOCKED_PROCESS_PATH;

LIST_ENTRY g_headOfBlockedPaths = { 0 };
ULONG g_valueDataSize = 0;

FAST_MUTEX g_guard;

class RecursionExpectedFastMutex {
    FAST_MUTEX guard;
    FAST_MUTEX mutex;
    ULONG recursionCount;
public:
    RecursionExpectedFastMutex() : recursionCount(0) { ExInitializeFastMutex(&guard), ExInitializeFastMutex(&mutex); }
    void Lock() { ExAcquireFastMutex(&guard); recursionCount == 0 ? ExAcquireFastMutex(&mutex), ++recursionCount : ++recursionCount; ExReleaseFastMutex(&guard); }
    void Unlock() { ExAcquireFastMutex(&guard); recursionCount > 0 ? --recursionCount == 0 ? ExReleaseFastMutex(&mutex), 0 : 0 : 0; ExReleaseFastMutex(&guard); }

    ~RecursionExpectedFastMutex() {
        ExReleaseFastMutex(&mutex);
    }
};

class AutoLock {
    PFAST_MUTEX pGuard;
public:
    AutoLock(PFAST_MUTEX pGuard) : pGuard(pGuard) { ExAcquireFastMutex(pGuard); }
    ~AutoLock() { ExReleaseFastMutex(pGuard); }
};

UNICODE_STRING g_registryPath = { 0 };
OBJECT_ATTRIBUTES g_driverRegistryAttribs = { 0 };

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT, PIRP pIrp);
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT, PIRP pIrp);
void DriverUnload(PDRIVER_OBJECT DriverObject);

NTSTATUS CompleteIrp(PIRP pIrp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0);

void OnProcessNotify(PEPROCESS pProcess, HANDLE processId, PPS_CREATE_NOTIFY_INFO createInfo);

NTSTATUS DowncaseUnicodeString(OUT PUNICODE_STRING pDest, IN PCUNICODE_STRING pSource);
NTSTATUS GetPathFromCmd(OUT PUNICODE_STRING pDest, IN PCUNICODE_STRING pSource);

NTSTATUS ReadSettingsFromRegistry(RegistryValues value);
NTSTATUS WriteSettingsToRegistry(RegistryValues value);

template<typename T>
NTSTATUS ReadSettingDwordValue(IN HANDLE hKey, IN const UNICODE_STRING& valueName, IN RegistryValues value, OUT T& settingValue, IN T defaultSettingValue);

void ClearProcPathsList();

NTSTATUS NormalizePath(OUT UNICODE_STRING& path, IN const UNICODE_STRING& rawPath);
NTSTATUS RemovePath(const UNICODE_STRING& path);
NTSTATUS AddPath(const UNICODE_STRING& path);

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath) {
    NTSTATUS status = STATUS_SUCCESS;
    __debugbreak();
    pDriverObject->DriverUnload = DriverUnload;
    pDriverObject->MajorFunction[IRP_MJ_CREATE] = pDriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

    PDEVICE_OBJECT pDeviceObject = nullptr;

    InitializeListHead(&g_headOfBlockedPaths);
    ExInitializeFastMutex(&g_guard);

    do {
        
        g_registryPath.Buffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, pRegistryPath->Length + sizeof(WCHAR), PROCBLOCK_POOL_TAG);

        if (!g_registryPath.Buffer) {
            status = STATUS_NO_MEMORY;
            break;
        }

        g_registryPath.MaximumLength = pRegistryPath->Length + sizeof(WCHAR);
        RtlCopyUnicodeString(&g_registryPath, pRegistryPath);

        InitializeObjectAttributes(&g_driverRegistryAttribs, &g_registryPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);

        // Check if it's works, otherwise add (A;;GA;;;SY)
        UNICODE_STRING sddl = RTL_CONSTANT_STRING(L"D:P(A;;GRGW;;;BA)");

        status = IoCreateDeviceSecure(pDriverObject, 0, const_cast<PUNICODE_STRING>(&DEVICE_NAME), FILE_DEVICE_UNKNOWN, 0, TRUE, &sddl, nullptr, &pDeviceObject);

        if (!NT_SUCCESS(status)) {
            KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n", status));
            break;
        }

        pDeviceObject->Flags |= DO_DIRECT_IO;

        status = IoCreateSymbolicLink(const_cast<PUNICODE_STRING>(&SYMBOLIC_LINK), const_cast<PUNICODE_STRING>(&DEVICE_NAME));

        if (!NT_SUCCESS(status)) {
            KdPrint((DRIVER_PREFIX "failed to create symbolic link (0x%08X)\n", status));
            break;
        }

        g_valueDataSize = 0;

        status = ReadSettingsFromRegistry(RegistryValues::DataMaxSize);
        REMEMBER_BAD_STATUS_IF_NEED(ReadSettingsFromRegistry(RegistryValues::Paths));
        REMEMBER_BAD_STATUS_IF_NEED(ReadSettingsFromRegistry(RegistryValues::BlockingEnabled));

        if (!NT_SUCCESS(status)) {
            KdPrint((DRIVER_PREFIX "failed to read settings from registry (0x%08X)\n", status));
            break;
        }

        status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);

        if (!NT_SUCCESS(status)) {
            KdPrint((DRIVER_PREFIX "failed to register process callback (0x%08X)\n", status));
            break;
        }

    } while (false);

    if (!NT_SUCCESS(status)) {
        ClearProcPathsList();

        IoDeleteSymbolicLink(const_cast<PUNICODE_STRING>(&SYMBOLIC_LINK));

        if (pDeviceObject)
            IoDeleteDevice(pDeviceObject);
    }

    return status;
}

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT, PIRP pIrp) { 
    return CompleteIrp(pIrp);
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT, PIRP pIrp) {
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);
    ULONG_PTR returnedSize = 0;

    switch (pStack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_SET_SETTINGS: {
        if (pStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(PROC_BLOCK_SETTINGS)) {
            status = STATUS_INVALID_BUFFER_SIZE;
            break;
        }

        PPROC_BLOCK_SETTINGS pSettings = (PPROC_BLOCK_SETTINGS)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority);

        if (!pSettings) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        {
            AutoLock guard(&g_guard);
            g_settings = *pSettings;
            REMEMBER_BAD_STATUS_IF_NEED(WriteSettingsToRegistry(RegistryValues::BlockingEnabled));
            REMEMBER_BAD_STATUS_IF_NEED(WriteSettingsToRegistry(RegistryValues::DataMaxSize));
        }
        break;
    }

    case IOCTL_GET_SETTINGS: {
        if (pStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(PROC_BLOCK_SETTINGS)) {
            status = STATUS_INVALID_BUFFER_SIZE;
            break;
        }

        PPROC_BLOCK_SETTINGS pSettings = (PPROC_BLOCK_SETTINGS)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority);

        if (!pSettings) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        {
            AutoLock guard(&g_guard);

            *pSettings = g_settings;
        }
        break;
    }

    case IOCTL_SET_PATH: {
        if (pStack->Parameters.DeviceIoControl.OutputBufferLength - sizeof(PathBuffer) + sizeof(CHAR) 
            > g_settings.maxPathsSize - (g_valueDataSize > MIN_DATA_SIZE ? g_valueDataSize : sizeof(END_OF_REG_MULTI_SZ))) 
        {
            status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        PathBuffer* pBuffer = (PathBuffer*)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority);

        if (!pBuffer) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        UNICODE_STRING rawPath = { 0 }, path = { 0 };
        RtlInitUnicodeString(&rawPath, (PCWSTR)pBuffer->buffer);

        if (!NT_SUCCESS(status = NormalizePath(path, rawPath)))
            break;

        if (pBuffer->add)
            status = AddPath(path);
        else {
            status = RemovePath(path);
            ExFreePoolWithTag(path.Buffer, PROCBLOCK_POOL_TAG);
        }

        break;
    }

    case IOCTL_GET_PATHS: {
        if (pStack->Parameters.DeviceIoControl.InputBufferLength != sizeof(ULONG)) {
            status = STATUS_INVALID_BUFFER_SIZE;
            break;
        }

        ULONG firstIndex = *(ULONG*)pIrp->AssociatedIrp.SystemBuffer;

        CHAR* pBuffer = (CHAR*)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority);

        if (!pBuffer) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        ULONG restOfBufferLen = pStack->Parameters.DeviceIoControl.OutputBufferLength;

        {
            AutoLock guard(&g_guard);

            if (IsListEmpty(&g_headOfBlockedPaths))
                break;

            LIST_ENTRY* pListItem = g_headOfBlockedPaths.Flink;
            
            while (firstIndex && pListItem != &g_headOfBlockedPaths) {
                pListItem = pListItem->Flink;
                --firstIndex;
            }

            if (pListItem == &g_headOfBlockedPaths)
                break;

            do {
                UNICODE_STRING& path = ((PBLOCKED_PROCESS_PATH)pListItem)->path;
                ULONG length = path.MaximumLength;

                if (length > restOfBufferLen) {
                    status = STATUS_BUFFER_OVERFLOW;
                    break;
                }

                RtlCopyMemory(pBuffer, path.Buffer, length);
                pBuffer += length;
                restOfBufferLen -= length;
                returnedSize += length;

            } while ((pListItem = pListItem->Flink) != &g_headOfBlockedPaths);
        }

        break;
    }

    case IOCTL_CLEAR_PATHS: {
        AutoLock guard(&g_guard);
        ClearProcPathsList();
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    return CompleteIrp(pIrp, status, returnedSize);
}

void DriverUnload(PDRIVER_OBJECT pDriverObject) {
    PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);

    {
        AutoLock guard(&g_guard);

        ClearProcPathsList();
    }

    IoDeleteSymbolicLink(const_cast<PUNICODE_STRING>(&SYMBOLIC_LINK));
    IoDeleteDevice(pDriverObject->DeviceObject);
}

NTSTATUS CompleteIrp(PIRP pIrp, NTSTATUS status, ULONG_PTR info) {
    pIrp->IoStatus.Status = status;
    pIrp->IoStatus.Information = info;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return status;
}

NTSTATUS NormalizePath(OUT UNICODE_STRING& path, IN const UNICODE_STRING& rawPath) {
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING downcasedPath = { 0 };

    do {
        if (!NT_SUCCESS(status = DowncaseUnicodeString(&downcasedPath, &rawPath)))
            break;

        status = GetPathFromCmd(&path, &downcasedPath);
    } while (false);

    if (downcasedPath.Buffer)
        ExFreePoolWithTag(downcasedPath.Buffer, PROCBLOCK_POOL_TAG);

    return status;
}


void OnProcessNotify(PEPROCESS, HANDLE, PPS_CREATE_NOTIFY_INFO createInfo) {
    UNICODE_STRING path = { 0 };

    if (createInfo && g_settings.isEnabled) {
        do {
            {
                AutoLock guard(&g_guard);
                if (IsListEmpty(&g_headOfBlockedPaths))
                    break;
            }
            
            if (!NT_SUCCESS(NormalizePath(path, *createInfo->CommandLine)))
                break;
            
            {
                AutoLock guard(&g_guard);

                if (IsListEmpty(&g_headOfBlockedPaths) || !g_settings.isEnabled)
                    break;

                LIST_ENTRY* pListItem = g_headOfBlockedPaths.Flink;

                do {
                    if (!RtlCompareUnicodeString(&path, &((PBLOCKED_PROCESS_PATH)pListItem)->path, FALSE))
                        createInfo->CreationStatus = STATUS_ACCESS_DENIED;

                } while ((pListItem = pListItem->Flink) != &g_headOfBlockedPaths && NT_SUCCESS(createInfo->CreationStatus));
            }
        } while (false);
    }

    if (path.Buffer)
        ExFreePoolWithTag(path.Buffer, PROCBLOCK_POOL_TAG);
}

// Destination string must be freed manualy after finishing of using
NTSTATUS DowncaseUnicodeString(OUT PUNICODE_STRING pDest, IN PCUNICODE_STRING pSource) {
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    WCHAR * p = pSource->Buffer
        , * s = nullptr;

    do {
        if (!p)
            break;

        pDest->Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, pSource->Length + sizeof(WCHAR), PROCBLOCK_POOL_TAG);

        if (!pDest->Buffer)
            break;
       
        pDest->Length = pSource->Length;
        pDest->MaximumLength = pSource->Length + sizeof(WCHAR);

        s = pDest->Buffer;

        while (*p)
            *s++ = RtlDowncaseUnicodeChar(*p++);

        *s = '\0';

        status = STATUS_SUCCESS;

    } while (false);

    return status;
}

// Expects downcased UNICODE_STRING as the source
NTSTATUS GetPathFromCmd(OUT PUNICODE_STRING pDest, IN PCUNICODE_STRING pSource) {
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    WCHAR * s = pSource->Buffer                      // Start path
        , * se = s + pSource->Length / sizeof(WCHAR) // Source buffer End
        , * e = nullptr                              // End path
        , * dp = nullptr;                            // Destination buffer Pointer
    USHORT length = 0;
    bool quotationed = false;

    do {
        if (!s)
            break;

        // source, for example
        // \??\c:\1\test.exe[ -some_param] | "c:\1\test.exe"[ -someparam] | c:\1\test.exe[ -some_param] | "\\some_host\test.exe"[ -someparam] | \\some_host\test.exe[ -someparam]
        while (s < se && !(*s >= L'a' && *s <= L'z' && *(s + 1) == L':') && !(*s == L'\\' && *s + 1 == L'\\')) {
            if (*s == L'\"')
                quotationed = true;
            ++s;
        }

        if (s == se)
            break;

        e = s;

        // if there was an opening quotation mark
        if (quotationed) {
            while (e < se && *e != L'\"')
                ++e;

            // Malformed cmd
            if (e == se)
                break;
        }
        else
            while (e < se && *e != L' ')
                ++e;
        
        length = (USHORT)((e - s) * sizeof(WCHAR));

        pDest->Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, length + sizeof(WCHAR), PROCBLOCK_POOL_TAG);

        if (!pDest->Buffer)
            break;

        pDest->Length = length;
        pDest->MaximumLength = length + sizeof(WCHAR);

        dp = pDest->Buffer;

        while (s < e)
            *dp++ = *s++;
        
        *dp = L'\0';

        status = STATUS_SUCCESS;

    } while (false);

    return status;
}

NTSTATUS ReadSettingsFromRegistry(RegistryValues value) {
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE hKey = NULL;

    status = ZwOpenKey(&hKey, KEY_ALL_ACCESS, &g_driverRegistryAttribs);
    
    if (NT_SUCCESS(status)) {
        switch (value) {
        case RegistryValues::BlockingEnabled:
            status = ReadSettingDwordValue(hKey, REGISTRY_BLOCKING_ENABLED, value, g_settings.isEnabled, false);
            break;
        case RegistryValues::DataMaxSize:
            status = ReadSettingDwordValue(hKey, REGISTRY_PATHS_MAX_SIZE, value, g_settings.maxPathsSize, DEFAULT_MAX_DATA_SIZE);
            break;
        case RegistryValues::Paths: {
            PKEY_VALUE_PARTIAL_INFORMATION pKeyValueInfo = nullptr;
            ULONG size = 0;

            // Clear current blocked paths list
            ClearProcPathsList();

            status = ZwQueryValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_BLOCKED_PATHS), KeyValuePartialInformation, nullptr, 0, &size);

            // Value key doesn't exist or data is corrupted
            if (status != STATUS_BUFFER_TOO_SMALL || (status == STATUS_BUFFER_TOO_SMALL && size > sizeof(KEY_VALUE_PARTIAL_INFORMATION) + g_settings.maxPathsSize - sizeof(UCHAR))) {
                status = WriteSettingsToRegistry(RegistryValues::Paths);

                if (NT_SUCCESS(status))
                    g_valueDataSize = MIN_DATA_SIZE;

                break;
            }

            pKeyValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, size, PROCBLOCK_POOL_TAG);

            if (!pKeyValueInfo) {
                status = STATUS_NO_MEMORY;
                break;
            }

            status = ZwQueryValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_BLOCKED_PATHS), KeyValuePartialInformation, pKeyValueInfo, size, &size);

            if (NT_SUCCESS(status)) {
                UCHAR* s = nullptr;

                s = pKeyValueInfo->Data;
                while (*(WCHAR*)s != L'\0') {
                    PBLOCKED_PROCESS_PATH pPath = nullptr;
                    WCHAR* pBuffer = nullptr;

                    pPath = (PBLOCKED_PROCESS_PATH)ExAllocatePoolWithTag(PagedPool, sizeof(BLOCKED_PROCESS_PATH), PROCBLOCK_POOL_TAG);

                    if (!pPath) {
                        status = STATUS_NO_MEMORY;
                        break;
                    }

                    // First get the Length and MaxLength fields values of BLOCKED_PROCESS_PATH::path
                    RtlInitUnicodeString(&pPath->path, (WCHAR*)s);

                    pBuffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, pPath->path.MaximumLength, PROCBLOCK_POOL_TAG);

                    if (!pBuffer) {
                        ExFreePoolWithTag(pPath, PROCBLOCK_POOL_TAG);
                        status = STATUS_NO_MEMORY;
                        break;
                    }

                    RtlCopyMemory(pBuffer, s, pPath->path.MaximumLength);
                    pPath->path.Buffer = pBuffer;

                    InsertTailList(&g_headOfBlockedPaths, (LIST_ENTRY*)pPath);
                    s += pPath->path.MaximumLength;
                }

                g_valueDataSize = pKeyValueInfo->DataLength;
            }

            ExFreePoolWithTag(pKeyValueInfo, PROCBLOCK_POOL_TAG);

            break;
        }
        default:
            status = STATUS_NOT_IMPLEMENTED;
            break;
        }
    }

    if (hKey)
        ZwClose(hKey);

    return status;
}

template<typename T>
NTSTATUS ReadSettingDwordValue(IN HANDLE hKey, IN const UNICODE_STRING& valueName, IN RegistryValues value, OUT T& settingValue, IN T defaultSettingValue) {
    NTSTATUS status = STATUS_SUCCESS;
    PKEY_VALUE_PARTIAL_INFORMATION pKeyValueInfo = nullptr;
    ULONG size = 0;

    settingValue = defaultSettingValue;

    do {
        status = ZwQueryValueKey(hKey, const_cast<PUNICODE_STRING>(&valueName), KeyValuePartialInformation, nullptr, 0, &size);

        if (status != STATUS_BUFFER_TOO_SMALL || !(status == STATUS_BUFFER_TOO_SMALL && size == sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG) - sizeof(UCHAR))) {
            status = WriteSettingsToRegistry(value);
            break;
        }

        pKeyValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, size, PROCBLOCK_POOL_TAG);

        if (!pKeyValueInfo) {
            status = STATUS_NO_MEMORY;
            break;
        }

        status = ZwQueryValueKey(hKey, const_cast<PUNICODE_STRING>(&valueName), KeyValuePartialInformation, pKeyValueInfo, size, &size);

        if (NT_SUCCESS(status))
            settingValue = (T) * (ULONG*)pKeyValueInfo->Data;

        ExFreePoolWithTag(pKeyValueInfo, PROCBLOCK_POOL_TAG);
    } while (false);

    return status;
}

NTSTATUS WriteSettingsToRegistry(RegistryValues value) {
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    HANDLE hKey = NULL;

    status = ZwOpenKey(&hKey, KEY_WRITE, &g_driverRegistryAttribs);

    if (NT_SUCCESS(status)) {
        switch (value) {
        case RegistryValues::BlockingEnabled: {
            ULONG isEnabled = g_settings.isEnabled ? 1 : 0;
            status = ZwSetValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_BLOCKING_ENABLED), 0, REG_DWORD, &isEnabled, sizeof(isEnabled));

            if (!NT_SUCCESS(status)) {
                ZwDeleteValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_BLOCKING_ENABLED));
                g_settings.isEnabled = false;
            }
            break;
        }
        case RegistryValues::DataMaxSize: {
            status = ZwSetValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_PATHS_MAX_SIZE), 0, REG_DWORD, &g_settings.maxPathsSize, sizeof(g_settings.maxPathsSize));

            if (!NT_SUCCESS(status)) {
                ZwDeleteValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_PATHS_MAX_SIZE));
                g_settings.maxPathsSize = DEFAULT_MAX_DATA_SIZE;
            }
            break;
        }
        case RegistryValues::Paths: {
            WCHAR* pPaths = (WCHAR*)ExAllocatePoolWithTag(PagedPool, g_valueDataSize, PROCBLOCK_POOL_TAG);

            if (!pPaths) {
                status = STATUS_NO_MEMORY;
                break;
            }

            if (IsListEmpty(&g_headOfBlockedPaths))
                *pPaths = L'\0', * (pPaths + 1) = L'\0';
            else {
                LIST_ENTRY* pListItem = g_headOfBlockedPaths.Flink;
                CHAR* pTempPath = (CHAR*)pPaths;
                PUNICODE_STRING pItemPath = nullptr;
                USHORT length = 0;

                do {
                    pItemPath = &((PBLOCKED_PROCESS_PATH)pListItem)->path;
                    length = pItemPath->MaximumLength;

                    RtlCopyMemory(pTempPath, pItemPath->Buffer, length);
                    pTempPath += length;
                } while ((pListItem = pListItem->Flink) != &g_headOfBlockedPaths);

                *(WCHAR*)pTempPath = L'\0';
            }

            status = ZwSetValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_BLOCKED_PATHS), 0, REG_MULTI_SZ, pPaths, g_valueDataSize);

            if (!NT_SUCCESS(status)) {
                ZwDeleteValueKey(hKey, const_cast<PUNICODE_STRING>(&REGISTRY_BLOCKED_PATHS));
                ClearProcPathsList();
            }
            break;
        }
        default:
            status = STATUS_NOT_IMPLEMENTED;
            break;
        }
    }

    if (hKey)
        ZwClose(hKey);

    return status;
}

NTSTATUS AddPath(const UNICODE_STRING& path) {
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PBLOCKED_PROCESS_PATH pBlockedProcessPath = nullptr;

    do {
        pBlockedProcessPath = (PBLOCKED_PROCESS_PATH)ExAllocatePoolWithTag(PagedPool, sizeof(BLOCKED_PROCESS_PATH), PROCBLOCK_POOL_TAG);

        if (!pBlockedProcessPath) {
            status = STATUS_NO_MEMORY;
            break;
        }
        
        pBlockedProcessPath->path = path;

        {
            AutoLock guard(&g_guard);
            if ((g_valueDataSize > MIN_DATA_SIZE ? g_valueDataSize : sizeof(END_OF_REG_MULTI_SZ)) + pBlockedProcessPath->path.Length + sizeof(WCHAR) > g_settings.maxPathsSize) {
                status = STATUS_BUFFER_OVERFLOW;
                ExFreePoolWithTag(pBlockedProcessPath, PROCBLOCK_POOL_TAG);
                break;
            }
            
            InsertTailList(&g_headOfBlockedPaths, (LIST_ENTRY*)pBlockedProcessPath);
            g_valueDataSize += g_valueDataSize > MIN_DATA_SIZE
                            ? pBlockedProcessPath->path.MaximumLength 
                            : pBlockedProcessPath->path.Length;

            status = WriteSettingsToRegistry(RegistryValues::Paths);
        }
        
    } while (false);

    return status;
}

NTSTATUS RemovePath(const UNICODE_STRING& path) {
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    do {
        if (path.Length + sizeof(WCHAR) > g_settings.maxPathsSize) {
            status = STATUS_BAD_DATA;
            break;
        }

        {
            AutoLock guard(&g_guard);

            if (IsListEmpty(&g_headOfBlockedPaths)) {
                status = STATUS_BAD_DATA;
                break;
            }

            LIST_ENTRY* pListItem = g_headOfBlockedPaths.Flink;

            do {
                if (!RtlCompareUnicodeString(&path, &((PBLOCKED_PROCESS_PATH)pListItem)->path, FALSE)) {
                    RemoveEntryList(pListItem);
                    g_valueDataSize -= ((PBLOCKED_PROCESS_PATH)pListItem)->path.Length - sizeof(WCHAR);
                    ExFreePoolWithTag(((PBLOCKED_PROCESS_PATH)pListItem)->path.Buffer, PROCBLOCK_POOL_TAG);
                    ExFreePoolWithTag(pListItem, PROCBLOCK_POOL_TAG);
                    break;
                }
            } while ((pListItem = pListItem->Flink) != &g_headOfBlockedPaths);

            if (IsListEmpty(&g_headOfBlockedPaths))
                g_valueDataSize = MIN_DATA_SIZE;
            
            status = WriteSettingsToRegistry(RegistryValues::Paths);
        }

    } while (false);

    return status;
}

void ClearProcPathsList() {
    LIST_ENTRY* pListHead = &g_headOfBlockedPaths
        , * pTempListHead = nullptr;

    do {
        if (IsListEmpty(pListHead))
            break;

        do {
            pListHead = RemoveHeadList(&g_headOfBlockedPaths);
            ExFreePoolWithTag(((PBLOCKED_PROCESS_PATH)pListHead)->path.Buffer, PROCBLOCK_POOL_TAG);
            pTempListHead = pListHead->Flink;
            ExFreePoolWithTag(pListHead, PROCBLOCK_POOL_TAG);

        } while (pTempListHead != &g_headOfBlockedPaths);
    } while (false);
}
