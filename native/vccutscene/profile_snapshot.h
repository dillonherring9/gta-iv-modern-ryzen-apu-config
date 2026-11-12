/* Immutable startup snapshot of our own INI. No Windows profile API is used:
 * this code runs under the DLL loader lock before the DualSense worker starts.
 * Read-only after initialization. Never modifies the settings file. */
#ifndef VC_PROFILE_SNAPSHOT_H
#define VC_PROFILE_SNAPSHOT_H
#include <windows.h>
#include <stdint.h>
#define VC_INI_MAX (1024u * 1024u)
static char vc_ini_data[VC_INI_MAX + 1];
static DWORD vc_ini_len;
static int vc_ini_status; /* 1 loaded, 0 absent, -1 unreadable/oversized */
static void vc_profile_load(const char *path) {
    vc_ini_len = 0; vc_ini_status = 0; vc_ini_data[0] = 0;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) { if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) vc_ini_status = -1; return; }
    LARGE_INTEGER z; DWORD n = 0;
    if (!GetFileSizeEx(f, &z) || z.QuadPart < 0 || z.QuadPart > VC_INI_MAX ||
        !ReadFile(f, vc_ini_data, (DWORD)z.QuadPart, &n, NULL) || n != (DWORD)z.QuadPart) {
        CloseHandle(f); vc_ini_status = -1; return;
    }
    CloseHandle(f); vc_ini_len = n; vc_ini_status = 1;
    /* Windows profile files can use UTF-16LE BOM. Keys and numbers are ASCII;
     * replace other code points with '?' without changing numeric content. */
    if (n >= 2 && (unsigned char)vc_ini_data[0] == 0xff && (unsigned char)vc_ini_data[1] == 0xfe) {
        DWORD o = 0; for (DWORD i = 2; i + 1 < n; i += 2) vc_ini_data[o++] = vc_ini_data[i+1] ? '?' : vc_ini_data[i];
        n = vc_ini_len = o;
    } else if (n >= 3 && (unsigned char)vc_ini_data[0] == 0xef && (unsigned char)vc_ini_data[1] == 0xbb && (unsigned char)vc_ini_data[2] == 0xbf) {
        for (DWORD i = 3; i < n; i++) vc_ini_data[i - 3] = vc_ini_data[i]; n -= 3; vc_ini_len = n;
    }
    vc_ini_data[n] = 0;
}
static int vc_isspace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static char vc_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static int vc_name_eq(const char *a, const char *end, const char *b) {
    while (a < end && vc_isspace(*a)) a++;
    while (end > a && vc_isspace(end[-1])) end--;
    while (a < end && *b && vc_lower(*a) == vc_lower(*b)) { a++; b++; }
    return a == end && *b == 0;
}
static int vc_number(const char *p, const char *end) {
    while (p < end && vc_isspace(*p)) p++;
    if (p < end && (*p == '\'' || *p == '"')) p++;
    int neg = 0; if (p < end && (*p == '-' || *p == '+')) { neg = *p == '-'; p++; }
    unsigned base = 10; if (p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    uint32_t value = 0;
    for (; p < end; p++) {
        unsigned d; char c = vc_lower(*p);
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0'); else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10); else break;
        if (d >= base) break; value = value * base + d;
    }
    return (int)(neg ? 0u - value : value);
}
static int vc_profile_int(const char *section, const char *key, int fallback, const char *path) {
    (void)path; const char *p = vc_ini_data, *end = p + vc_ini_len; int in = 0;
    while (p < end) {
        const char *line = p; while (p < end && *p != '\n' && *p != '\r') p++;
        const char *e = p; while (p < end && (*p == '\n' || *p == '\r')) p++;
        while (line < e && vc_isspace(*line)) line++;
        if (line == e || *line == ';') continue;
        if (*line == '[') {
            const char *close = line + 1; while (close < e && *close != ']') close++;
            in = close < e && vc_name_eq(line + 1, close, section); continue;
        }
        if (!in) continue;
        const char *eq = line; while (eq < e && *eq != '=') eq++;
        if (eq < e && vc_name_eq(line, eq, key)) {
            const char *value = eq + 1; while (value < e && vc_isspace(*value)) value++;
            return value == e ? fallback : vc_number(value, e);
        }
    }
    return fallback;
}
#endif
