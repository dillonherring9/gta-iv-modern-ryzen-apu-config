/* Direct Win32 log I/O: never hold a CRT FILE-table lock across UAL's file
 * hooks, which query the DLL loader. Formatting finishes before file I/O. */
#ifndef VC_STARTUP_LOG_H
#define VC_STARTUP_LOG_H
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static void vc_write_log(const char *path, const char *data, DWORD n, int append) {
    HANDLE f = CreateFileA(path, append ? FILE_APPEND_DATA : GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(f, data, n, &written, NULL);
    CloseHandle(f);
}
static void vc_log_v(const char *path, const char *prefix, const char *format, va_list args) {
    char line[4096]; SYSTEMTIME time;
    GetLocalTime(&time);
    int begin = snprintf(line, sizeof line, "[%02d:%02d:%02d] %s",
                         time.wHour, time.wMinute, time.wSecond, prefix);
    if (begin < 0 || begin >= (int)sizeof line - 2) return;
    int count = vsnprintf(line + begin, sizeof line - (size_t)begin - 2, format, args);
    if (count < 0) return;
    size_t end = (size_t)begin + (size_t)count;
    if (end > sizeof line - 2) end = sizeof line - 2;
    line[end++] = '\n';
    vc_write_log(path, line, (DWORD)end, 1);
}
#endif
