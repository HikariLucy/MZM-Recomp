#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#include <fileapi.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <malloc.h>

#if defined(__cplusplus)
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)

#ifndef CreateFileW
static inline HANDLE CreateFileW_uwp(
    const wchar_t* lpFileName,
    unsigned long dwDesiredAccess,
    unsigned long dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    unsigned long dwCreationDisposition,
    unsigned long dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    CREATEFILE2_EXTENDED_PARAMETERS params = {};
    params.dwSize = sizeof(params);
    params.dwFileAttributes = dwFlagsAndAttributes & 0x0000FFFF;
    params.dwFileFlags = dwFlagsAndAttributes & 0xFFFF0000;
    params.lpSecurityAttributes = lpSecurityAttributes;
    params.hTemplateFile = hTemplateFile;
    return CreateFile2(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, &params);
}
#define CreateFileW CreateFileW_uwp
#endif

#ifndef CreateFileA
static inline HANDLE CreateFileA_uwp(
    const char* lpFileName,
    unsigned long dwDesiredAccess,
    unsigned long dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    unsigned long dwCreationDisposition,
    unsigned long dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    if (!lpFileName) return INVALID_HANDLE_VALUE;
    int len = MultiByteToWideChar(CP_UTF8, 0, lpFileName, -1, NULL, 0);
    if (len <= 0) return INVALID_HANDLE_VALUE;
    wchar_t* wbuf = (wchar_t*)_alloca(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, lpFileName, -1, wbuf, len);
    return CreateFileW_uwp(wbuf, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                           dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}
#define CreateFileA CreateFileA_uwp
#endif

#ifndef STARTUPINFOA
typedef struct _STARTUPINFOA {
    unsigned long cb;
    char* lpReserved;
    char* lpDesktop;
    char* lpTitle;
    unsigned long dwX;
    unsigned long dwY;
    unsigned long dwXSize;
    unsigned long dwYSize;
    unsigned long dwXCountChars;
    unsigned long dwYCountChars;
    unsigned long dwFillAttribute;
    unsigned long dwFlags;
    unsigned short wShowWindow;
    unsigned short cbReserved2;
    unsigned char* lpReserved2;
    void* hStdInput;
    void* hStdOutput;
    void* hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;
#endif

#ifndef STARTF_USESTDHANDLES
#define STARTF_USESTDHANDLES 0x00000100
#endif
#ifndef CREATE_NO_WINDOW
#define CREATE_NO_WINDOW 0x08000000
#endif

#ifndef CreateProcessA
static inline int CreateProcessA_uwp(
    const char*, char*, void*, void*, int, unsigned long, void*, const char*, void*, void*)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}
#define CreateProcessA CreateProcessA_uwp
#endif

#ifndef GetExitCodeProcess
static inline int GetExitCodeProcess_uwp(void*, unsigned long* lpExitCode) {
    if (lpExitCode) *lpExitCode = 1;
    return 1;
}
#define GetExitCodeProcess GetExitCodeProcess_uwp
#endif

#ifndef WaitForSingleObject
static inline unsigned long WaitForSingleObject_uwp(void* hHandle, unsigned long dwMilliseconds) {
    return WaitForSingleObjectEx(hHandle, dwMilliseconds, FALSE);
}
#define WaitForSingleObject WaitForSingleObject_uwp
#endif

#ifndef OPENFILENAMEA
typedef struct tagOFNA {
    unsigned long lStructSize;
    void* hwndOwner;
    void* hInstance;
    const char* lpstrFilter;
    char* lpstrCustomFilter;
    unsigned long nMaxCustFilter;
    unsigned long nFilterIndex;
    char* lpstrFile;
    unsigned long nMaxFile;
    char* lpstrFileTitle;
    unsigned long nMaxFileTitle;
    const char* lpstrInitialDir;
    const char* lpstrTitle;
    unsigned long Flags;
    unsigned short nFileOffset;
    unsigned short nFileExtension;
    const char* lpstrDefExt;
    void* lCustData;
    void* lpfnHook;
    const char* lpTemplateName;
    void* pvReserved;
    unsigned long dwReserved;
    unsigned long FlagsEx;
} OPENFILENAMEA, *LPOPENFILENAMEA;
#endif

#ifndef OFN_FILEMUSTEXIST
#define OFN_FILEMUSTEXIST 0x00001000
#endif
#ifndef OFN_PATHMUSTEXIST
#define OFN_PATHMUSTEXIST 0x00000800
#endif
#ifndef OFN_NOCHANGEDIR
#define OFN_NOCHANGEDIR 0x00000008
#endif
#ifndef MB_OK
#define MB_OK 0x00000000L
#endif
#ifndef MB_ICONERROR
#define MB_ICONERROR 0x00000010L
#endif
#ifndef MB_ICONWARNING
#define MB_ICONWARNING 0x00000030L
#endif

#ifndef GetOpenFileNameA
static inline int GetOpenFileNameA_uwp(void*) { return 0; }
#define GetOpenFileNameA GetOpenFileNameA_uwp
#endif

#ifndef MessageBoxA
static inline int MessageBoxA_uwp(void*, const char*, const char*, unsigned int) { return 0; }
#define MessageBoxA MessageBoxA_uwp
#endif

#ifndef LoadLibraryA
static inline void* LoadLibraryA_uwp(const char*) { return NULL; }
#define LoadLibraryA LoadLibraryA_uwp
#endif

#endif // !WINAPI_PARTITION_DESKTOP
#endif // __cplusplus
#endif // _WIN32
