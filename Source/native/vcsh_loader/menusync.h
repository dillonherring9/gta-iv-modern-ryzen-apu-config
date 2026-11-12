/* menusync.h - Settings > Audio slider "DualSense Controller Speaker Volume" (step 171, Claude 2026-11-03).
 *
 * The row (update/common/data/frontend_menus.xml, SCR_AUDIO + SCR_TAUDIO, label MO_DS5SPKVOL, scaler 21, slider) uses
 * the native preference PREF_BASS (id 28). VERIFIED FILE (GTAIV.exe 1.2.0.59, 08759a55...): name table 0xF8DD48 entry
 * {28, "PREF_BASS"}; no instruction references prefs[28] (0x1160CB8) directly - the game never reads, applies or
 * defaults it. It is only zeroed (0x8B5966, all 165), bulk-loaded (0x8B97DB) and bulk-saved (0x8BE97C) with the other
 * preferences, and written by the frontend when the row is adjusted (0x8BB333 / mouse 0x8BB667; FusionFix routes
 * native ids to the same array). Current screen = 0x1160C40 (SCR_AUDIO 7, SCR_TAUDIO 60, enum table 0xF8D8C0/0xF8DA68).
 *
 * Link with [ControllerAudio] SpeakerVolume (the source of truth, percent 0-100; slider position 0-20 = 5 % steps, step 180):
 * - start: slider := round(SpeakerVolume / 5);
 * - value changed while an Audio screen is open = the player moved the slider: volume applies at once (ctlaudio.h)
 *   and SpeakerVolume is written to VCScriptHook.ini 0.4 s later (temp file + rename, other lines kept);
 * - value changed anywhere else (the game zeroing/loading its settings) = restored to the current volume. */
#include <stdlib.h>

#ifndef MS_PREFS   /* test.c points these at its own simulated game */
#define MS_PREFS      0x1160C48u
#define MS_SCREEN     0x1160C40u
#endif
#define MS_PREF_ID    28
#define MS_MAXPOS     20

static int ms_entry_ok(uintptr_t entry, uint32_t id, const char *name) {
    if (!readable(entry, 8)) return 0;
    const uint32_t *e = (const uint32_t *)entry; size_t n = strlen(name) + 1;
    return e[0] == id && readable(e[1], n) && !memcmp((const char *)(uintptr_t)e[1], name, n);
}
static int ms_verify(void) {
    return ms_entry_ok(0xF8DD48 + MS_PREF_ID * 8, MS_PREF_ID, "PREF_BASS") &&
           ms_entry_ok(0xF8D8C0, 7, "SCR_AUDIO") && ms_entry_ok(0xF8DA68, 60, "SCR_TAUDIO") &&
           bytes_eq(0x8B5966, "B9 A5 00 00 00 BF 48 0C 16 01 F3 AB") &&                       /* zero all prefs */
           bytes_eq(0x8B97DB, "B9 A5 00 00 00 8D 74 24 14 BF 48 0C 16 01 F3 A5") &&             /* load all prefs */
           bytes_eq(0x8BB33F, "A1 40 0C 16 01 83 C4 04 8D 04 40 8B 04 C5 A0 33 9D 01");          /* screens[current] */
}
static int ms_pos_of(int pct) { int p = (pct + 2) / 5; return p < 0 ? 0 : p > MS_MAXPOS ? MS_MAXPOS : p; }

/* Rewrites only the SpeakerVolume line of [ControllerAudio] (adds it, or the section, if missing). */
static int ms_save(const char *ini, int pct) {
    char *buf = NULL; DWORD n = 0;
    HANDLE h = CreateFileA(ini, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(h, NULL);
        if (sz != INVALID_FILE_SIZE && sz < (1u << 20) && (buf = malloc(sz + 1)) && ReadFile(h, buf, sz, &n, NULL)) buf[n] = 0; else n = 0;
        CloseHandle(h);
    }
    char *out = malloc((size_t)n + 256); if (!out) { free(buf); return 0; }
    size_t o = 0; int in_sec = 0, seen = 0, done = 0;
    const char *p = buf ? buf : "", *end = p + n;
    while (p < end) {
        const char *e = memchr(p, '\n', (size_t)(end - p)), *nl = e ? e + 1 : end, *q = p;
        while (q < nl && (*q == ' ' || *q == '\t')) q++;
        if (q < nl && *q == '[') {
            if (in_sec && !done) { o += (size_t)sprintf(out + o, "SpeakerVolume=%d\r\n", pct); done = 1; }
            in_sec = (size_t)(nl - q) >= 17 && !_strnicmp(q, "[ControllerAudio]", 17); seen |= in_sec;
        } else if (in_sec && !done && (size_t)(nl - q) >= 13 && !_strnicmp(q, "SpeakerVolume", 13)) {
            const char *r = q + 13; while (r < nl && (*r == ' ' || *r == '\t')) r++;
            if (r < nl && *r == '=') { o += (size_t)sprintf(out + o, "SpeakerVolume=%d\r\n", pct); done = 1; p = nl; continue; }
        }
        memcpy(out + o, p, (size_t)(nl - p)); o += (size_t)(nl - p); p = nl;
    }
    if (!done) {
        if (o && out[o - 1] != '\n') o += (size_t)sprintf(out + o, "\r\n");
        if (!seen) o += (size_t)sprintf(out + o, "[ControllerAudio]\r\n");
        o += (size_t)sprintf(out + o, "SpeakerVolume=%d\r\n", pct);
    }
    free(buf);
    char tmp[MAX_PATH + 8]; snprintf(tmp, sizeof tmp, "%s.tmp", ini);
    int ok = 0; DWORD w = 0;
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        ok = WriteFile(h, out, (DWORD)o, &w, NULL) && w == (DWORD)o && FlushFileBuffers(h);
        CloseHandle(h);
        ok = ok && MoveFileExA(tmp, ini, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) DeleteFileA(tmp);
    }
    free(out); return ok;
}

static DWORD WINAPI ms_thread(LPVOID _) {
    (void)_;
    volatile int32_t *pref = (volatile int32_t *)(MS_PREFS + 4 * MS_PREF_ID), *scr = (volatile int32_t *)MS_SCREEN;
    int last = ms_pos_of((int)lrintf(ca_spk_vol * 100.f)), dirty = 0, restores = 0; DWORD at = 0;
    *pref = last;
    llog("menu: Settings > Audio speaker slider linked to SpeakerVolume (position %d of %d)", last, MS_MAXPOS);
    for (;;) {
        Sleep(50);
        int cur = *pref, s = *scr;
        if (cur != last) {
            if ((s == 7 || s == 60) && cur >= 0 && cur <= MS_MAXPOS) {
                last = cur; ca_set_spk_vol(cur * 5); dirty = 1; at = GetTickCount();
            } else {
                *pref = last;
                if (restores++ < 8) llog("menu: slider kept at %d (game wrote %d, screen %d)", last, cur, s);
            }
        }
        if (dirty && GetTickCount() - at >= 400) {
            dirty = 0;
            llog("menu: SpeakerVolume=%d%% %s", last * 5, ms_save(g_ini, last * 5) ? "saved" : "NOT saved (write failed)");
        }
    }
    return 0;
}
static void ms_init(void) {
    if (!ms_verify()) { llog("menu: GTAIV.exe menu tables not as expected - speaker slider inactive"); return; }
    HANDLE h = CreateThread(NULL, 0, ms_thread, NULL, 0, NULL); if (h) CloseHandle(h);
}
