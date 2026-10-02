#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#include <fileapi.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <commdlg.h>
#include <malloc.h>

#if defined(__cplusplus)
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)

#ifndef CreateFileW
static inline HANDLE CreateFileW_uwp(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
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
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
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

#ifndef CreateProcessA
static inline BOOL CreateProcessA_uwp(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
    (void)lpApplicationName; (void)lpCommandLine; (void)lpProcessAttributes;
    (void)lpThreadAttributes; (void)bInheritHandles; (void)dwCreationFlags;
    (void)lpEnvironment; (void)lpCurrentDirectory; (void)lpStartupInfo;
    (void)lpProcessInformation;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
#define CreateProcessA CreateProcessA_uwp
#endif

#ifndef GetExitCodeProcess
static inline BOOL GetExitCodeProcess_uwp(HANDLE hProcess, LPDWORD lpExitCode) {
    (void)hProcess;
    if (lpExitCode) *lpExitCode = 1;
    return TRUE;
}
#define GetExitCodeProcess GetExitCodeProcess_uwp
#endif

#ifndef WaitForSingleObject
static inline DWORD WaitForSingleObject_uwp(HANDLE hHandle, DWORD dwMilliseconds) {
    return WaitForSingleObjectEx(hHandle, dwMilliseconds, FALSE);
}
#define WaitForSingleObject WaitForSingleObject_uwp
#endif

#ifndef GetOpenFileNameA
static inline BOOL GetOpenFileNameA_uwp(LPOPENFILENAMEA) { return FALSE; }
#define GetOpenFileNameA GetOpenFileNameA_uwp
#endif

#ifndef MessageBoxA
static inline int MessageBoxA_uwp(HWND, LPCSTR, LPCSTR, UINT) { return 0; }
#define MessageBoxA MessageBoxA_uwp
#endif

#ifndef LoadLibraryA
static inline HMODULE LoadLibraryA_uwp(LPCSTR) { return NULL; }
#define LoadLibraryA LoadLibraryA_uwp
#endif

#endif // !WINAPI_PARTITION_DESKTOP
#endif // __cplusplus
#endif // _WIN32
