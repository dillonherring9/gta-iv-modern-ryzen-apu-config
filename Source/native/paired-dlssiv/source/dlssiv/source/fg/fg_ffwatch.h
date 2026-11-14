// FusionFix's limiter changed in its own menu. LatReadLimiterCfg reads it only at load, so this is the conditions' own copy,
// re-read when the cfg's (or the Custom ini's) write time moves, once a window. It repeats that function's rule (the reference)
// and runs only where its first read agrees with it. No function-local static or const table: lists are built on the stack.
#pragma once

struct FgFfWatch
{
    char cfg[MAX_PATH];             // "" none found
    char ini[MAX_PATH];             // "" none
    FILETIME cfgT, iniT;
    int haveCfgT, haveIniT;
    int ff51;                       // 1: FusionFix 5.1.0's numbering (or version unread); 0: 5.0.x
    int cap, rel;                   // fps cap (0 none), or Custom -N (rel: the refresh less N)
    volatile LONG packed;           // cap | rel << 16, for the conditions' step on another thread:
                                    // one store, one load, never a cap with another read's rel
    int on;                         // paths found and the first read agreed with the latency's
    int changes;
};
// LatReadLimiterCfg's rule. customLimit: the ini's FpsLimit (-1 where missing).
static inline void FgFfFromPreset(int preset, int ff51, int customSeen, int customLimit, int* cap, int* rel)
{
    int k51[13] = { 0, -1, 30, 40, 50, 60, 75, 100, 120, 144, 165, 200, 240 };          // OFF, Custom, ...
    int k50[15] = { 0, 0, 0, -1, 30, 40, 50, 60, 75, 100, 120, 144, 165, 200, 240 };    // TOGGLE, HOLD, OFF, Custom, ...
    const int nCaps = ff51 ? 13 : 15, kCustom = ff51 ? 1 : 3;
    if (preset >= nCaps) preset = nCaps - 1;
    int c = 0, r = 0;
    if (preset > kCustom && preset < nCaps) c = ff51 ? k51[preset] : k50[preset];
    else if (preset == kCustom)
    {
        const int limit = customSeen ? customLimit : -1;
        if (limit < 0) { r = -limit; c = r; } else c = limit;
    }
    *cap = r ? 0 : c; *rel = r;
}
static inline int FgFfTimeMoved(const char* path, FILETIME* last, int* have);
// Returns 1 where it differs from the last read. A read taken while the menu writes is dropped: both write times are asked before
// and after it, and the next window reads again.
static inline int FgFfRead(FgFfWatch& w)
{
    FILETIME c0 = w.cfgT, i0 = w.iniT; int hc = w.haveCfgT, hi = w.haveIniT;
    const int preset = w.cfg[0] ? (int)GetPrivateProfileIntA("FRAMELIMIT", "FpsLimitPreset", (UINT)-1, w.cfg) : -1;
    int seen = 0, limit = -1;
    if (w.ini[0] && GetFileAttributesA(w.ini) != INVALID_FILE_ATTRIBUTES) { limit = (int)GetPrivateProfileIntA("FRAMELIMIT", "FpsLimit", (UINT)-1, w.ini); seen = 1; }
    int cap = 0, rel = 0;
    FgFfFromPreset(preset, w.ff51, seen, limit, &cap, &rel);
    if (FgFfTimeMoved(w.cfg, &c0, &hc) | FgFfTimeMoved(w.ini, &i0, &hi)) { w.haveCfgT = 0; w.haveIniT = 0; return 0; }
    const int diff = cap != w.cap || rel != w.rel;
    w.cap = cap; w.rel = rel;
    InterlockedExchange(&w.packed, (LONG)((cap & 0xFFFF) | (rel << 16)));
    if (diff) w.changes++;
    return diff;
}
// 1 where it moved (or was first read).
static inline int FgFfTimeMoved(const char* path, FILETIME* last, int* have)
{
        // a missing file is not a move: an editor that saves by rename is caught at the next window
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!path[0] || !GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return 0;
    const int moved = !*have || CompareFileTime(&a.ftLastWriteTime, last) != 0;
    *last = a.ftLastWriteTime; *have = 1;
    return moved;
}
// Returns 1 where the limiter changed.
static inline int FgFfWatchStep(FgFfWatch& w)
{
    if (!w.on) return 0;
    const int a = FgFfTimeMoved(w.cfg, &w.cfgT, &w.haveCfgT), b = FgFfTimeMoved(w.ini, &w.iniT, &w.haveIniT);
    if (!a && !b) return 0;
    return FgFfRead(w);
}
// The watch runs only where its first read agrees with LatReadLimiterCfg's (latCap, latRel). Returns 1 where it runs.
static inline int FgFfWatchStart(FgFfWatch& w, int latCap, int latRel)
{
    w.on = 0; w.cap = 0; w.rel = 0; w.changes = 0; w.packed = 0;
    if (!w.cfg[0]) return 0;
    FgFfTimeMoved(w.cfg, &w.cfgT, &w.haveCfgT); FgFfTimeMoved(w.ini, &w.iniT, &w.haveIniT);
    FgFfRead(w); w.changes = 0;
    w.on = (w.cap == latCap && w.rel == latRel) ? 1 : 0;
    return w.on;
}
static inline void FgFfText(int cap, int rel, char* b, size_t n)
{
    if (rel > 0) _snprintf_s(b, n, _TRUNCATE, "Custom -%d (the refresh less %d)", rel, rel);
    else if (cap > 0) _snprintf_s(b, n, _TRUNCATE, "%d fps", cap);
    else _snprintf_s(b, n, _TRUNCATE, "off");
}
