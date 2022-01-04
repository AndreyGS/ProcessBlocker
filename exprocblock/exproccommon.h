#pragma once

#define PROC_BLOCK_DEVICE 0x8000

#define IOCTL_SET_SETTINGS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x8000, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)

#define IOCTL_GET_SETTINGS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x8001, METHOD_OUT_DIRECT, FILE_READ_ACCESS)

#define IOCTL_SET_PATH CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x8002, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)

#define IOCTL_GET_PATHS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x8003, METHOD_OUT_DIRECT, FILE_READ_ACCESS)

#define IOCTL_CLEAR_PATHS CTL_CODE(PROC_BLOCK_DEVICE \
    , 0x8004, METHOD_NEITHER, FILE_WRITE_ACCESS)

typedef struct {
    bool isEnabled;
    ULONG maxPathsSize;
} PROC_BLOCK_SETTINGS, * PPROC_BLOCK_SETTINGS;

typedef struct {
    bool add;                   // add = true - path will be added, add = false - path will be deleted 
    CHAR buffer[ANYSIZE_ARRAY];
} PathBuffer;
