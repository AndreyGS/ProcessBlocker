#pragma once

#define PROC_BLOCK_DEVICE 0x8000

// Cause all highest-level drivers are called in a nonarbitrary thread context at IRQL == PASSIVE_LEVEL
// we can apply METHOD_NEITHER to all of our CTL_CODEs
// There were METHOD_IN(OUT)_DIRECT before but why we should use safe methods where we don't have a risk?

#define IOCTL_SET_SETTINGS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x800, METHOD_NEITHER, FILE_WRITE_ACCESS)

#define IOCTL_GET_SETTINGS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x801, METHOD_NEITHER, FILE_READ_ACCESS)

#define IOCTL_SET_PATH CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x802, METHOD_NEITHER, FILE_WRITE_ACCESS)

#define IOCTL_GET_PATHS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x803, METHOD_NEITHER, FILE_READ_ACCESS)

#define IOCTL_CLEAR_PATHS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x804, METHOD_NEITHER, FILE_WRITE_ACCESS)

typedef struct {
    bool isEnabled;
    ULONG maxPathsSize;
} PROC_BLOCK_SETTINGS, * PPROC_BLOCK_SETTINGS;

typedef struct {
    bool add;                   // add = true - path will be added, add = false - path will be deleted 
    CHAR buffer[ANYSIZE_ARRAY];
} PathBuffer;
