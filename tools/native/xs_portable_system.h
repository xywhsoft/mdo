#ifndef XS_PORTABLE_SYSTEM_H
#define XS_PORTABLE_SYSTEM_H
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/statvfs.h>
#endif

uint64 xsCurrentProcessId(void)
{
#if defined(_WIN32)
    return (uint64)GetCurrentProcessId();
#else
    return (uint64)getpid();
#endif
}

bool xsDiskAvailableBytes(cstr Directory, uint64* Available)
{
    if (Available) *Available = 0;
    if (!Directory || !Directory[0] || !Available) return false;
#if defined(_WIN32)
    int Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Directory, -1, NULL, 0);
    if (Count <= 0 || Count > 32768) return false;
    WCHAR* Wide = (WCHAR*)malloc((size_t)Count * sizeof(WCHAR));
    if (!Wide) return false;
    ULARGE_INTEGER Bytes;
    bool Ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Directory, -1, Wide, Count) &&
        GetDiskFreeSpaceExW(Wide, &Bytes, NULL, NULL);
    free(Wide);
    if (Ok) *Available = (uint64)Bytes.QuadPart;
    return Ok;
#else
    struct statvfs Info;
    if (statvfs(Directory, &Info) != 0) return false;
    uint64 Unit = (uint64)(Info.f_frsize ? Info.f_frsize : Info.f_bsize);
    uint64 Blocks = (uint64)Info.f_bavail;
    *Available = Unit && Blocks > UINT64_MAX / Unit ? UINT64_MAX : Unit * Blocks;
    return true;
#endif
}
#endif
