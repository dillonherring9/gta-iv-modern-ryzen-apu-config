/* Native virtual XInput pad from the DualSense's own HID input reports (replaces ViGEm under Proton; step 161b).
 * Proton/Wine presents the DualSense only as raw HID (hidraw), XInput sees nothing (VERIFIED probe 2026-11-02).
 * A reader thread opens the pad read-only/shared (the DualSense mod keeps its own handle for output), parses
 * USB 0x01 (64 B), BT full 0x31 (78 B) and BT simple 0x01 (10 B) reports, and XInputGetState(0) of GTAIV.exe and
 * GTA_IV_DualSense.asi returns it. Other users / no DualSense -> the real xinput9_1_0 functions. */
#include <setupapi.h>
#include <hidsdi.h>
typedef struct { WORD b; BYTE lt, rt; SHORT lx, ly, rx, ry; } VPad;
typedef struct { DWORD pkt; VPad g; } VState;
typedef struct { BYTE type, sub; WORD flags; VPad g; struct { WORD l, r; } vib; } VCaps;
typedef DWORD (WINAPI *XGetState)(DWORD, VState *);
typedef DWORD (WINAPI *XSetState)(DWORD, void *);
typedef DWORD (WINAPI *XGetCaps)(DWORD, DWORD, VCaps *);
static XGetState real_get; static XSetState real_set; static XGetCaps real_caps;
static CRITICAL_SECTION v_cs; static VState v_state; static volatile LONG v_live; static DWORD v_last;
static SHORT axis(BYTE v, int inv) { int x = ((int)v - 128) * 257; if (inv) x = -x - 1; if (x > 32767) x = 32767; if (x < -32768) x = -32768; return (SHORT)x; }
/* caps = HID input report length: 64 = USB (0x01 full report), else Bluetooth: 0x31 full or 0x01 simple
 * (Wine pads BT simple reports to the 78-byte BT length - VERIFIED probe 2026-11-02, hat neutral 8 at byte 5). */
static DWORD v_caps_len;
static void v_parse(const BYTE *r, DWORD n) {
    const BYTE *s; BYTE b0, b1, l2, r2;
    if (r[0] == 0x31 && n >= 12) { s = r + 2; b0 = s[7]; b1 = s[8]; l2 = s[4]; r2 = s[5]; }
    else if (r[0] == 0x01 && v_caps_len == 64 && n >= 12) { s = r + 1; b0 = s[7]; b1 = s[8]; l2 = s[4]; r2 = s[5]; }
    else if (r[0] == 0x01 && n >= 10) { s = r + 1; b0 = s[4]; b1 = s[5]; l2 = s[7]; r2 = s[8]; }
    else return;
    VPad g; memset(&g, 0, sizeof g);
    g.lx = axis(s[0], 0); g.ly = axis(s[1], 1); g.rx = axis(s[2], 0); g.ry = axis(s[3], 1); g.lt = l2; g.rt = r2;
    static const WORD hat[8] = { 0x1, 0x1 | 0x8, 0x8, 0x2 | 0x8, 0x2, 0x2 | 0x4, 0x4, 0x1 | 0x4 };   /* up down left right */
    if ((b0 & 0xF) < 8) g.b |= hat[b0 & 0xF];
    if (b0 & 0x10) g.b |= 0x4000;  /* square   -> X */
    if (b0 & 0x20) g.b |= 0x1000;  /* cross    -> A */
    if (b0 & 0x40) g.b |= 0x2000;  /* circle   -> B */
    if (b0 & 0x80) g.b |= 0x8000;  /* triangle -> Y */
    if (b1 & 0x01) g.b |= 0x0100;  /* L1 -> LB */
    if (b1 & 0x02) g.b |= 0x0200;  /* R1 -> RB */
    if (b1 & 0x10) g.b |= 0x0020;  /* create -> BACK */
    if (b1 & 0x20) g.b |= 0x0010;  /* options -> START */
    if (b1 & 0x40) g.b |= 0x0040;  /* L3 */
    if (b1 & 0x80) g.b |= 0x0080;  /* R3 */
    EnterCriticalSection(&v_cs);
    if (memcmp(&g, &v_state.g, sizeof g)) { v_state.g = g; v_state.pkt++; }
    v_last = GetTickCount(); LeaveCriticalSection(&v_cs);
}
static HANDLE v_open_ex(char *path, size_t cap, DWORD access) {
    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO di = SetupDiGetClassDevsA(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA d; d.cbSize = sizeof d; HANDLE h = INVALID_HANDLE_VALUE;
    for (DWORD i = 0; h == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(di, NULL, &guid, i, &d); i++) {
        char buf[1024]; SP_DEVICE_INTERFACE_DETAIL_DATA_A *det = (void *)buf; det->cbSize = sizeof *det;
        if (!SetupDiGetDeviceInterfaceDetailA(di, &d, det, sizeof buf, NULL, NULL)) continue;
        char low[1024]; size_t k = 0; for (; det->DevicePath[k] && k < sizeof low - 1; k++) low[k] = (char)tolower((unsigned char)det->DevicePath[k]); low[k] = 0;
        if (!strstr(low, "054c") || !(strstr(low, "0ce6") || strstr(low, "0df2")) || strstr(low, "&col02") || strstr(low, "&col03")) continue;
        h = CreateFileA(det->DevicePath, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) { strncpy(path, det->DevicePath, cap - 1); path[cap - 1] = 0; }
    }
    SetupDiDestroyDeviceInfoList(di); return h;
}
static HANDLE v_open(char *path, size_t cap) { return v_open_ex(path, cap, GENERIC_READ); }
static DWORD WINAPI v_thread(LPVOID _) {
    (void)_; int logged_missing = 0;
    for (;;) {
        char path[1024]; HANDLE h = v_open(path, sizeof path);
        if (h == INVALID_HANDLE_VALUE) { if (!logged_missing++) llog("virtual pad: no DualSense HID device yet; retrying"); Sleep(2000); continue; }
        PHIDP_PREPARSED_DATA pp; HIDP_CAPS caps; DWORD len = 78;
        if (HidD_GetPreparsedData(h, &pp)) { if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.InputReportByteLength) len = caps.InputReportByteLength; HidD_FreePreparsedData(pp); }
        v_caps_len = len;
        llog("virtual pad: reading %s (input report %lu bytes) -> XInput user 0", path, len);
        BYTE r[512]; DWORD n; int first = 1; logged_missing = 0;
        while (ReadFile(h, r, len < sizeof r ? len : sizeof r, &n, NULL)) {
            if (first && n) { llog("virtual pad: first report id 0x%02X, %lu bytes", r[0], n); first = 0; }
            InterlockedExchange(&v_live, 1); v_parse(r, n);
        }
        llog("virtual pad: read failed (%lu) - controller gone; waiting", GetLastError());
        InterlockedExchange(&v_live, 0); CloseHandle(h); Sleep(1000);
    }
    return 0;
}
static int v_on(void) { return v_live && GetTickCount() - v_last < 3000; }
static DWORD WINAPI h_get(DWORD u, VState *s) {
    if (u == 0 && v_on() && s) { EnterCriticalSection(&v_cs); *s = v_state; LeaveCriticalSection(&v_cs); return ERROR_SUCCESS; }
    return real_get ? real_get(u, s) : ERROR_DEVICE_NOT_CONNECTED;
}
static DWORD WINAPI h_set(DWORD u, void *vib) {
    if (u == 0 && v_on()) return ERROR_SUCCESS;   /* game rumble: the DualSense mod drives the motors itself */
    return real_set ? real_set(u, vib) : ERROR_DEVICE_NOT_CONNECTED;
}
static DWORD WINAPI h_caps(DWORD u, DWORD f, VCaps *c) {
    if (u == 0 && v_on() && c) { memset(c, 0, sizeof *c); c->type = 1; c->sub = 1; c->g.b = 0xF3FF; c->g.lt = c->g.rt = 0xFF;
        c->g.lx = c->g.ly = c->g.rx = c->g.ry = (SHORT)0xFFC0; c->vib.l = c->vib.r = 0xFFFF; return ERROR_SUCCESS; }
    return real_caps ? real_caps(u, f, c) : ERROR_DEVICE_NOT_CONNECTED;
}
/* patch one module's import thunks for xinput*.dll by name (in memory only) */
static int patch_iat(HMODULE m, const char *who) {
    if (!m) return 0;
    BYTE *base = (BYTE *)m; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; int n = 0;
    if (!dd.VirtualAddress) return 0;
    for (IMAGE_IMPORT_DESCRIPTOR *id = (void *)(base + dd.VirtualAddress); id->Name; id++) {
        const char *dll = (const char *)(base + id->Name);
        if (_strnicmp(dll, "xinput", 6) || !id->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *on = (void *)(base + id->OriginalFirstThunk), *ft = (void *)(base + id->FirstThunk);
        for (; on->u1.AddressOfData; on++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(on->u1.Ordinal)) continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + on->u1.AddressOfData))->Name; void *to = NULL;
            if (!strcmp(fn, "XInputGetState")) { if (!real_get) real_get = (XGetState)ft->u1.Function; to = (void *)h_get; }
            else if (!strcmp(fn, "XInputSetState")) { if (!real_set) real_set = (XSetState)ft->u1.Function; to = (void *)h_set; }
            else if (!strcmp(fn, "XInputGetCapabilities")) { if (!real_caps) real_caps = (XGetCaps)ft->u1.Function; to = (void *)h_caps; }
            if (!to || (void *)ft->u1.Function == to) continue;
            DWORD old; if (!VirtualProtect(&ft->u1.Function, 4, PAGE_READWRITE, &old)) continue;
            ft->u1.Function = (DWORD_PTR)to; VirtualProtect(&ft->u1.Function, 4, old, &old); n++;
        }
    }
    llog("virtual pad: %d XInput import(s) of %s redirected", n, who); return n;
}

/* CE 1.2.0.59 resolves XInput at 0x6A0D60 with GetProcAddress into globals (VERIFIED: 0x6A0DAB):
 * XInputGetState -> 0x18B7DC0, XInputSetState -> 0x18B802C, XInputGetCapabilities -> 0x18B8028. */
static int g_ce_xinput = -1;
static void swap_ce_xinput(void) {
    if (g_ce_xinput < 0) g_ce_xinput = bytes_eq(0x6A0DAB, "68 E8 13 FA 00 50 FF D6 68 F8 13 FA 00 FF 35 30 80 8B 01 A3 C0 7D 8B 01 FF D6 68 D0 13 FA 00 FF 35 30 80 8B 01 A3 2C 80 8B 01 FF D6 83 3D C0 7D 8B 01 00 A3 28 80 8B 01");
    if (!g_ce_xinput) return;
    void **gs = (void **)0x18B7DC0, **ss = (void **)0x18B802C, **gc = (void **)0x18B8028; int n = 0;
    if (*gs && *gs != (void *)h_get) { if (!real_get) real_get = (XGetState)*gs; *gs = (void *)h_get; n++; }
    if (*ss && *ss != (void *)h_set) { if (!real_set) real_set = (XSetState)*ss; *ss = (void *)h_set; n++; }
    if (*gc && *gc != (void *)h_caps) { if (!real_caps) real_caps = (XGetCaps)*gc; *gc = (void *)h_caps; n++; }
    if (n) llog("virtual pad: %d CE XInput pointer(s) redirected", n);
}
/* menus/loading screens may not run the game-process slot: also re-check from a light thread (aligned 4-byte stores) */
static DWORD WINAPI v_swap_thread(LPVOID _) { (void)_; for (;;) { swap_ce_xinput(); Sleep(250); } return 0; }
