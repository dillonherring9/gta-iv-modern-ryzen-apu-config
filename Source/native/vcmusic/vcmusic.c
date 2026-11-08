/* VCMusic.asi - native port of AVCNG's .NET music scripts for GTA IV CE 1.2.0.59
 * (Vice City Actual Next Gen port). Claude, 2026-10-31.
 *
 * Replaces, without .NET / ScriptHook:
 *   VWantedMusic  (ItsClonkAndre)  chase music from N wanted stars
 *   DeathMusicIV  (ItsClonkAndre)  music when the player dies / is near death
 *   VFlyingMusic  (ItsClonkAndre)  music while flying
 * Audio through bass.dll (plugins/VCMusic/bass.dll). Game state through CE natives:
 * the CE native table lookup (stdcall fn(hash) -> handler) and CE native hashes
 * (IVSDK .NET natives.txt). Per-frame callback by chaining the game-loop call slot
 * that ProjectThunderCE also chains (0x5C2A41). Every address is verified by bytes
 * before use; on mismatch the plugin logs and stays inactive.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

/* ---------------- log ---------------- */
static char g_dir[MAX_PATH];      /* ...\plugins */
static char g_logpath[MAX_PATH];
static void vlog(const char *fmt, ...) {
    FILE *f = fopen(g_logpath, "a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

/* ---------------- BASS ---------------- */
typedef DWORD HSTREAM;
#define BASS_ATTRIB_VOL 2
#define BASS_UNICODE 0x80000000
#define BASS_ERROR_ALREADY 14
#define BASS_ACTIVE_STOPPED 0
#define BASS_ACTIVE_PLAYING 1
#define BASS_ACTIVE_PAUSED 3
static BOOL (WINAPI *pBASS_Init)(int, DWORD, DWORD, HWND, const void *);
static int (WINAPI *pBASS_ErrorGetCode)(void);
static HSTREAM (WINAPI *pBASS_StreamCreateFile)(BOOL, const void *, uint64_t, uint64_t, DWORD);
static BOOL (WINAPI *pBASS_StreamFree)(HSTREAM);
static BOOL (WINAPI *pBASS_ChannelPlay)(DWORD, BOOL);
static BOOL (WINAPI *pBASS_ChannelPause)(DWORD);
static DWORD (WINAPI *pBASS_ChannelIsActive)(DWORD);
static BOOL (WINAPI *pBASS_ChannelSetAttribute)(DWORD, DWORD, float);
static BOOL (WINAPI *pBASS_ChannelSlideAttribute)(DWORD, DWORD, float, DWORD);
static BOOL (WINAPI *pBASS_ChannelIsSliding)(DWORD, DWORD);

static int bass_load(void) {
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\VCMusic\\bass.dll", g_dir);
    HMODULE m = LoadLibraryA(p);
    if (!m) { vlog("bass.dll not found at %s (error %lu) - music disabled", p, GetLastError()); return 0; }
#define GETP(n) if (!(*(FARPROC *)&p##n = GetProcAddress(m, #n))) { vlog("bass.dll missing %s", #n); return 0; }
    GETP(BASS_Init) GETP(BASS_ErrorGetCode) GETP(BASS_StreamCreateFile) GETP(BASS_StreamFree)
    GETP(BASS_ChannelPlay) GETP(BASS_ChannelPause) GETP(BASS_ChannelIsActive)
    GETP(BASS_ChannelSetAttribute) GETP(BASS_ChannelSlideAttribute) GETP(BASS_ChannelIsSliding)
    if (!pBASS_Init(-1, 44100, 0, NULL, NULL)) {
        int e = pBASS_ErrorGetCode();
        if (e != BASS_ERROR_ALREADY) { vlog("BASS_Init failed (%d) - music disabled", e); return 0; }
        vlog("BASS already initialised in this process, sharing it");
    }
    return 1;
}

/* ---------------- CE natives ---------------- */
typedef struct {            /* CE scrNativeCallContext, layout taken from ProjectThunderCE */
    void *ret; uint32_t argc; void *args; uint32_t datacount;
    uint32_t fix[4]; uint8_t tmp[0x40]; uint32_t buf[32];
} NativeCtx;
typedef void (__cdecl *NativeFn)(NativeCtx *);
typedef NativeFn (__stdcall *GetNativeFn)(uint32_t);
static GetNativeFn g_getNative;
static uint32_t *g_nativeSize;

enum { N_GET_PLAYER_ID, N_GET_PLAYER_CHAR, N_DOES_CHAR_EXIST, N_IS_CHAR_DEAD, N_IS_PLAYER_DEAD,
       N_GET_CHAR_HEALTH, N_STORE_WANTED_LEVEL, N_IS_CHAR_IN_FLYING_VEHICLE, N_GET_CAR_CHAR_IS_USING,
       N_DOES_VEHICLE_EXIST, N_IS_VEHICLE_ON_ALL_WHEELS, N_IS_PAUSE_MENU_ACTIVE, N_GET_INT_STAT, N_SET_INT_STAT, N_COUNT };
static const uint32_t k_hash[N_COUNT] = {  /* CE hashes (IVSDK .NET data/natives.txt) */
    0x62E319C6, 0x511454A9, 0x46531797, 0x6A6B4F18, 0x12AE0E27,
    0x4B6C2256, 0x12AA6D71, 0x7FA763E8, 0x1B067237,
    0x67A42263, 0x4D460265, 0x6C4568A7, 0x48994D58, 0x1B64665B };
static NativeFn g_nat[N_COUNT];
static int g_natReady;

static uint32_t ncall(int id, int argc, const uint32_t *argv) {
    NativeCtx c; memset(&c, 0, sizeof c);
    c.ret = c.args = c.buf; c.argc = argc;
    for (int i = 0; i < argc; i++) c.buf[i] = argv[i];
    g_nat[id](&c);
    return c.buf[0];
}
static uint32_t n0(int id) { return ncall(id, 0, NULL); }
static uint32_t n1(int id, uint32_t a) { uint32_t v[1] = {a}; return ncall(id, 1, v); }
static uint32_t n2(int id, uint32_t a, uint32_t b) { uint32_t v[2] = {a, b}; return ncall(id, 2, v); }

static int natives_resolve(void) {
    if (g_natReady) return 1;
    if (!*g_nativeSize) return 0;                 /* table not built yet */
    for (int i = 0; i < N_COUNT; i++) {
        g_nat[i] = g_getNative(k_hash[i]);
        if (!g_nat[i]) { vlog("native %08X not found - music disabled", k_hash[i]); return -1; }
    }
    g_natReady = 1; vlog("natives resolved (%d)", N_COUNT); return 1;
}

/* ---------------- settings ---------------- */
static struct {
    int wantedOn, wantedStart, wantedVol, wantedFade, wantedNext;
    int deathOn, deathVol, deathFade, nearDeath, nearDeathHealth;
    int flyOn, flyVol, flyFade, flyResume;
} S;
static int ini(const char *sec, const char *key, int def) {
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\VCMusic.ini", g_dir);
    return GetPrivateProfileIntA(sec, key, def, p);
}
static void settings_load(void) {
    S.wantedOn = ini("Wanted", "Enabled", 1); S.wantedStart = ini("Wanted", "StartAt", 3);
    S.wantedVol = ini("Wanted", "Volume", 40); S.wantedFade = ini("Wanted", "FadingSpeed", 6000);
    S.wantedNext = ini("Wanted", "PlayAnotherTrackWhenDone", 1);
    S.deathOn = ini("Death", "Enabled", 1); S.deathVol = ini("Death", "Volume", 40);
    S.deathFade = ini("Death", "FadingSpeed", 3000); S.nearDeath = ini("Death", "PlayWhenNearDeath", 1);
    S.nearDeathHealth = ini("Death", "NearDeathHealth", 10);
    S.flyOn = ini("Flying", "Enabled", 1); S.flyVol = ini("Flying", "Volume", 60);
    S.flyFade = ini("Flying", "FadingSpeed", 2000); S.flyResume = ini("Flying", "ResumeWhereLeft", 1);
}

/* ---------------- tracks ---------------- */
typedef struct { char dir[16]; int count; char name[16][MAX_PATH]; } TrackList;
static TrackList T_wanted = {"Wanted"}, T_death = {"Death"}, T_fly = {"Flying"};
static void tracks_scan(TrackList *t) {
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\VCMusic\\%s\\*", g_dir, t->dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd); t->count = 0;
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *x = strrchr(fd.cFileName, '.');
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && x &&
            (!_stricmp(x, ".mp3") || !_stricmp(x, ".wav") || !_stricmp(x, ".ogg")) && t->count < 16)
            snprintf(t->name[t->count++], MAX_PATH, "%s\\VCMusic\\%s\\%s", g_dir, t->dir, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    vlog("%s: %d track(s)", t->dir, t->count);
}
static uint32_t g_rng;
static int rnd(int n) { g_rng = g_rng * 1103515245u + 12345u; return (int)((g_rng >> 16) % (unsigned)n); }

/* ---------------- music slots ---------------- */
typedef struct {
    TrackList *list; HSTREAM h; int last; int fadingOut; int *vol, *fade;
} Slot;
static Slot W = {&T_wanted, 0, -1, 0, &S.wantedVol, &S.wantedFade};
static Slot D = {&T_death, 0, -1, 0, &S.deathVol, &S.deathFade};
static Slot F = {&T_fly, 0, -1, 0, &S.flyVol, &S.flyFade};
static Slot *const ALL[3] = {&D, &W, &F};

static void slot_free(Slot *s) { if (s->h) { pBASS_StreamFree(s->h); s->h = 0; } s->fadingOut = 0; }
static int slot_start(Slot *s, int fadeIn) {
    if (!s->list->count) return 0;
    slot_free(s);
    int i = rnd(s->list->count);
    if (s->list->count > 1 && i == s->last) i = (i + 1) % s->list->count;
    s->last = i;
    s->h = pBASS_StreamCreateFile(FALSE, s->list->name[i], 0, 0, 0);
    if (!s->h) { vlog("cannot open %s (BASS error %d)", s->list->name[i], pBASS_ErrorGetCode()); return 0; }
    float v = *s->vol / 100.0f;
    pBASS_ChannelSetAttribute(s->h, BASS_ATTRIB_VOL, fadeIn ? 0.0f : v);
    pBASS_ChannelPlay(s->h, FALSE);
    if (fadeIn) pBASS_ChannelSlideAttribute(s->h, BASS_ATTRIB_VOL, v, *s->fade);
    return 1;
}
static void slot_resume(Slot *s) {      /* resume a paused (faded-out) stream */
    pBASS_ChannelSetAttribute(s->h, BASS_ATTRIB_VOL, 0.0f);
    pBASS_ChannelPlay(s->h, FALSE);
    pBASS_ChannelSlideAttribute(s->h, BASS_ATTRIB_VOL, *s->vol / 100.0f, *s->fade);
    s->fadingOut = 0;
}
static void slot_fadeout(Slot *s) {
    if (!s->h || s->fadingOut) return;
    pBASS_ChannelSlideAttribute(s->h, BASS_ATTRIB_VOL, 0.0f, *s->fade);
    s->fadingOut = 1;
}

static int audio_start(void);
static DWORD WINAPI watchdog(LPVOID);
/* ---------------- state machine (game thread) ---------------- */
enum { M_NONE, M_DEATH, M_WANTED, M_FLY };
static volatile LONG g_lastTick;      /* GetTickCount of last game frame */
static volatile LONG g_paused;        /* our streams paused by menu/watchdog */
static CRITICAL_SECTION g_cs;
static int g_active;                  /* plugin usable */
static DWORD g_nextPoll;
static int g_deathLatch;              /* death track already started for this death */

static void vc_pause_dialogue(int on);
static int g_pausedByUs[3];
static void pause_all(int on) {
    if (on == g_paused) return;
    for (int i = 0; i < 3; i++) {
        Slot *s = ALL[i];
        if (on) {
            g_pausedByUs[i] = s->h && pBASS_ChannelIsActive(s->h) == BASS_ACTIVE_PLAYING;
            if (g_pausedByUs[i]) pBASS_ChannelPause(s->h);
        } else if (g_pausedByUs[i] && s->h) {
            pBASS_ChannelPlay(s->h, FALSE); g_pausedByUs[i] = 0;
        }
    }
    vc_pause_dialogue(on);
    g_paused = on;
}

static void housekeeping(void) {
    /* finish fade-outs: wanted/death streams are freed, flying is kept paused to resume */
    for (int i = 0; i < 3; i++) {
        Slot *s = ALL[i];
        if (s->h && s->fadingOut && !pBASS_ChannelIsSliding(s->h, BASS_ATTRIB_VOL)) {
            if (s == &F && S.flyResume) { pBASS_ChannelPause(s->h); }
            else slot_free(s);
        }
    }
}

/* VC 2002 one-shot sounds (steps 178/179): a script sets int stat 284 (hidden, unused) to first | second << 8 (sound ids from
   VCMusic\VC2002\sounds.txt, "id file" per line); we play the first, then the second when the first has ended, and reset the stat.
   Ids: 1 mono_5 (ice_1 scene end), 10-24 gimme, 30-41 burg, 50-58 crust, 60-67 shaft, 70-77 band, 80-99 fud, 100-102 golf,
   110-124 hot1-15, 125-128 cheer1-4, 129-130 ooh1-2 (step 181); 140-148 strip_1-9 (step 182);
   255 = Hotring commentary tick, 254 = private-dance line request (first payment, then at most every 30 s). */
static char g_vcFile[256][40]; static int g_vcLoaded, g_vcNext, g_commLast, g_vcActiveId; static HSTREAM g_vcH; static DWORD g_commUntil, g_commAt, g_stripAt; static int g_stripLast;
static int g_vcDialoguePaused;
static void vc_stop_dialogue(void) {
    if (g_vcActiveId >= 220 && g_vcActiveId <= 227) {
        if (g_vcH && pBASS_ChannelIsActive(g_vcH)) pBASS_StreamFree(g_vcH);
        g_vcH = 0; g_vcActiveId = 0; g_vcNext = 0; g_vcDialoguePaused = 0;
    }
}
/* New station/phone lines share a bounded family; bank controls stay separate. */
static void vc_pause_dialogue(int on) {
    if (g_vcActiveId < 220 || g_vcActiveId > 227 || !g_vcH) { g_vcDialoguePaused = 0; return; }
    if (on) {
        g_vcDialoguePaused = pBASS_ChannelIsActive(g_vcH) == BASS_ACTIVE_PLAYING;
        if (g_vcDialoguePaused) pBASS_ChannelPause(g_vcH);
    } else if (g_vcDialoguePaused) {
        if (pBASS_ChannelIsActive(g_vcH) == BASS_ACTIVE_PAUSED) pBASS_ChannelPlay(g_vcH, FALSE);
        g_vcDialoguePaused = 0;
    }
}
static void vc_load_table(void) {
    char p[MAX_PATH], line[128]; unsigned id; char name[40];
    g_vcLoaded = 1; snprintf(p, sizeof p, "%s\\VCMusic\\VC2002\\sounds.txt", g_dir);
    FILE *f = fopen(p, "r"); if (!f) { vlog("VC2002 sounds.txt missing"); return; }
    int n = 0;
    while (fgets(line, sizeof line, f))
        if (line[0] != '#' && sscanf(line, "%u %39s", &id, name) == 2 && id > 0 && id < 256) { strcpy(g_vcFile[id], name); n++; }
    fclose(f); vlog("VC2002 sounds: %d entries", n);
}
static int g_vcNextVol;
static void vc_play(int id, int vol) {   /* vol: 1-100 % of [VC2002Sounds] Volume (step 190: positional 2002 lines); 0 = 100 % */
    if (id <= 0 || id > 255 || !g_vcFile[id][0]) { if (id) vlog("VC2002 sound %d unknown", id); return; }
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\VCMusic\\VC2002\\%s", g_dir, g_vcFile[id]);
    HSTREAM h = pBASS_StreamCreateFile(FALSE, p, 0, 0, 0x40000 /* BASS_STREAM_AUTOFREE */);
    if (!h) { vlog("VC2002 sound %s: BASS error %d", g_vcFile[id], pBASS_ErrorGetCode()); return; }
    /* Avoid orphaning a bank clip if a different VC sound replaces it. */
    if (g_vcActiveId >= 212 && g_vcActiveId <= 227 && g_vcH && pBASS_ChannelIsActive(g_vcH)) pBASS_StreamFree(g_vcH);
    g_vcDialoguePaused = 0;
    pBASS_ChannelSetAttribute(h, BASS_ATTRIB_VOL, ini("VC2002Sounds", "Volume", 90) / 100.0f * (vol > 0 && vol <= 100 ? vol / 100.0f : 1.0f));
    pBASS_ChannelPlay(h, FALSE); g_vcH = h; g_vcActiveId = id;
}
static void vc2002_sounds(void) {
    if (!ini("VC2002Sounds", "Enabled", 1)) return;
    if (!g_vcLoaded) vc_load_table();
    if (g_vcNext && (!g_vcH || !pBASS_ChannelIsActive(g_vcH))) { int n = g_vcNext; g_vcNext = 0; vc_play(n, g_vcNextVol); }
    uint32_t v = n1(N_GET_INT_STAT, 284); DWORD now = GetTickCount();
    if (v) n2(N_SET_INT_STAT, 284, 0);
    if (v == 252) { /* Stop only the new station/phone family. */
        vc_stop_dialogue();
        return;
    }
    if (v == 253) { /* Stop only this step's bank conversation; never stop other sound families. */
        if (g_vcActiveId >= 212 && g_vcActiveId <= 219) {
            if (g_vcH && pBASS_ChannelIsActive(g_vcH)) pBASS_StreamFree(g_vcH);
            g_vcH = 0; g_vcActiveId = 0; g_vcNext = 0;
        }
        return;
    }
    if (v == 255) g_commUntil = now + 1000;   /* step 181: Hotring race running (ovalring.sco sets it every HUD frame) */
    else if (v == 254) {                       /* step 182: private-dance payment (STRIP.sco); 2002 interiors.sc play_stripper_line */
        if ((!g_stripAt || now - g_stripAt >= 30000) && (!g_vcH || !pBASS_ChannelIsActive(g_vcH))) {
            int id = 140 + (int)((now / 13 + (now >> 6)) % 9);
            if (id == g_stripLast) id = id == 148 ? 140 : id + 1;
            g_stripLast = id; g_stripAt = now; vc_play(id, 0);
        }
        return;
    }
    else if (v) { int vol = (int)((v >> 16) & 0xFF); vc_play((int)(v & 0xFF), vol); g_vcNext = (int)((v >> 8) & 0xFF); g_vcNextVol = vol; return; }
    /* 2002 ovalring.sc: every 2.5 s, when nothing is playing, a random HOT1-15 / CHEER1-4 / OOH1-2 (ids 110-130), not the last one */
    if ((LONG)(g_commUntil - now) > 0 && (!g_vcH || !pBASS_ChannelIsActive(g_vcH)) && now - g_commAt >= 2500) {
        int id = 110 + (int)((now / 7 + (now >> 5)) % 21);
        if (id == g_commLast) id = id == 130 ? 110 : id + 1;
        g_commLast = id; g_commAt = now; vc_play(id, 0);
    }
}
static void game_tick(void) {
    DWORD now = GetTickCount();
    InterlockedExchange(&g_lastTick, (LONG)now);
    if (!g_active) return;
    if ((LONG)(now - g_nextPoll) < 0) return;      /* poll ~10x per second */
    g_nextPoll = now + 100;
    int r = natives_resolve();
    if (r < 0) { g_active = 0; return; }
    if (r == 0) return;
    if (!audio_start()) { g_active = 0; return; }

    EnterCriticalSection(&g_cs);
    if (n0(N_IS_PAUSE_MENU_ACTIVE) & 0xff) { pause_all(1); LeaveCriticalSection(&g_cs); return; }
    pause_all(0);
    housekeeping();
    vc2002_sounds();

    uint32_t player = n0(N_GET_PLAYER_ID), ped = 0;
    n2(N_GET_PLAYER_CHAR, player, (uint32_t)(uintptr_t)&ped);
    int want = M_NONE;
    if (ped && (n1(N_DOES_CHAR_EXIST, ped) & 0xff)) {
        int dead = (n1(N_IS_CHAR_DEAD, ped) & 0xff) || (n1(N_IS_PLAYER_DEAD, player) & 0xff);
        if (dead) vc_stop_dialogue();
        uint32_t health = 0, wanted = 0;
        n2(N_GET_CHAR_HEALTH, ped, (uint32_t)(uintptr_t)&health);
        n2(N_STORE_WANTED_LEVEL, player, (uint32_t)(uintptr_t)&wanted);
        /* CE player health is raw: 200 full, 100 dead -> usable = raw - 100 */
        int usable = (int)health - 100;
        int nearDeath = S.nearDeath && !dead && usable > 0 && usable <= S.nearDeathHealth;
        int flying = 0;
        if (!dead && (n1(N_IS_CHAR_IN_FLYING_VEHICLE, ped) & 0xff)) {
            uint32_t car = 0; n2(N_GET_CAR_CHAR_IS_USING, ped, (uint32_t)(uintptr_t)&car);
            flying = car && (n1(N_DOES_VEHICLE_EXIST, car) & 0xff) && !(n1(N_IS_VEHICLE_ON_ALL_WHEELS, car) & 0xff);
        }
        if (S.deathOn && (dead || nearDeath)) want = M_DEATH;
        else if (S.wantedOn && (int)wanted >= S.wantedStart) want = M_WANTED;
        else if (S.flyOn && flying) want = M_FLY;
        if (!dead && !nearDeath) g_deathLatch = 0;
    }

    /* death: one track per death, no restart while latched */
    if (want == M_DEATH) {
        if (!g_deathLatch) { g_deathLatch = 1; slot_start(&D, 1); }
    } else slot_fadeout(&D);
    /* wanted: loop through random tracks while the level holds */
    if (want == M_WANTED) {
        if (!W.h || W.fadingOut) slot_start(&W, 1);
        else if (S.wantedNext && pBASS_ChannelIsActive(W.h) == BASS_ACTIVE_STOPPED) slot_start(&W, 0);
    } else slot_fadeout(&W);
    /* flying: resume the same long track where it stopped */
    if (want == M_FLY) {
        if (F.h && pBASS_ChannelIsActive(F.h) == BASS_ACTIVE_PAUSED) slot_resume(&F);
        else if (F.h && F.fadingOut) slot_resume(&F);
        else if (!F.h || pBASS_ChannelIsActive(F.h) == BASS_ACTIVE_STOPPED) slot_start(&F, 1);
    } else slot_fadeout(&F);
    LeaveCriticalSection(&g_cs);
}

/* watchdog: game loop stalled (loading, alt-tab, menus not running the loop) -> pause */
static DWORD WINAPI watchdog(LPVOID _) {
    for (;;) {
        Sleep(100);
        if (!g_active || !g_natReady) continue;
        DWORD last = (DWORD)g_lastTick;
        if (last && GetTickCount() - last > 400) {
            EnterCriticalSection(&g_cs); pause_all(1); LeaveCriticalSection(&g_cs);
        }
    }
    return 0;
}

/* ---------------- hook ---------------- */
uintptr_t vcm_prev __asm__("vcm_prev");
__attribute__((force_align_arg_pointer)) void vcm_tick(void) __asm__("vcm_tick");
void vcm_tick(void) { game_tick(); }
__attribute__((naked)) static void hook_stub(void) {
    __asm__ volatile("pushal\n\tpushfl\n\tcall vcm_tick\n\tpopfl\n\tpopal\n\tjmp *vcm_prev\n");
}

static int readable(uintptr_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)a, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return a + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static int bytes_eq(uintptr_t a, const char *hex) {   /* "56 8B 35 ?? .." */
    const unsigned char *p = (const unsigned char *)a;
    if (!readable(a, 32)) return 0;
    for (int i = 0; *hex; i++) {
        while (*hex == ' ') hex++;
        if (!*hex) break;
        if (hex[0] != '?') { unsigned v; sscanf(hex, "%2x", &v); if (p[i] != v) return 0; }
        hex += 2;
    }
    return 1;
}

static void init(void) {
    settings_load();
    g_rng = GetTickCount() ^ GetCurrentProcessId();
    vlog("VCMusic 1.0 (native port of VWantedMusic/DeathMusicIV/VFlyingMusic)");
    /* CE 1.2.0.59: native lookup and game-loop call slot */
    const uintptr_t getNative = 0x86FCE0, slot = 0x5C2A41;
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000) { vlog("not GTAIV.exe - inactive"); return; }
    if (!bytes_eq(getNative, "56 8B 35 ?? ?? ?? ?? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10") ||
        !bytes_eq(slot - 5, "E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8") ||
        !bytes_eq(slot + 15, "B9 ?? ?? ?? ?? E8")) {
        vlog("unsupported GTAIV.exe (byte check failed) - music disabled"); return;
    }
    g_getNative = (GetNativeFn)getNative;
    g_nativeSize = *(uint32_t **)(getNative + 3);
    /* chain the call slot (bass.dll is loaded later, on the game thread, never under the loader lock) */
    DWORD old; VirtualProtect((void *)slot, 5, PAGE_EXECUTE_READWRITE, &old);
    vcm_prev = slot + 5 + *(int32_t *)(slot + 1);
    *(int32_t *)(slot + 1) = (int32_t)((uintptr_t)hook_stub - (slot + 5));
    VirtualProtect((void *)slot, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)slot, 5);
    vlog("game-loop hook at %08X (previous target %08X)", (unsigned)slot, (unsigned)vcm_prev);
    g_active = 1;
}

static int g_audioState;   /* 0 not tried, 1 ok, -1 failed */
static int audio_start(void) {
    if (g_audioState) return g_audioState > 0;
    g_audioState = -1;
    if (!bass_load()) return 0;
    tracks_scan(&T_wanted); tracks_scan(&T_death); tracks_scan(&T_fly);
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    g_audioState = 1; vlog("audio ready"); return 1;
}

__declspec(dllexport) void vcm_selftest(void) {   /* test harness only, never called by the game */
    settings_load(); if (!audio_start()) return;
    for (int k = 0; k < 3; k++) { Slot *s = ALL[k]; if (slot_start(s, 1)) { Sleep(1500);
        vlog("selftest %s playing=%lu", s->list->dir, pBASS_ChannelIsActive(s->h)); slot_fadeout(s); Sleep(*s->fade + 300);
        vlog("selftest %s faded, sliding=%d", s->list->dir, pBASS_ChannelIsSliding(s->h, BASS_ATTRIB_VOL)); slot_free(s); } }
    vc_load_table();   /* step 179: every VC2002 sound family, then a queued pair like a vendor exchange */
    int ids[] = {1, 10, 30, 50, 60, 70, 80, 100};
    for (int k = 0; k < 8; k++) { vc_play(ids[k], 0); Sleep(300); vlog("selftest VC2002 %d %s playing=%lu", ids[k], g_vcFile[ids[k]], g_vcH ? pBASS_ChannelIsActive(g_vcH) : 0);
        while (g_vcH && pBASS_ChannelIsActive(g_vcH)) Sleep(50); }
    vc_play(24, 0); g_vcNext = 41; DWORD t0 = GetTickCount();
    while (g_vcNext && GetTickCount() - t0 < 15000) { if (!pBASS_ChannelIsActive(g_vcH)) { int n = g_vcNext; g_vcNext = 0; vc_play(n, 0); vlog("selftest queue second %d started playing=%lu", n, pBASS_ChannelIsActive(g_vcH)); } Sleep(50); }
    vc_play(159, 40); Sleep(300); vlog("selftest step190 %d %s vol40 playing=%lu", 159, g_vcFile[159], g_vcH ? pBASS_ChannelIsActive(g_vcH) : 0); while (g_vcH && pBASS_ChannelIsActive(g_vcH)) Sleep(50); vc_play(211, 0); Sleep(300); vlog("selftest step190 %d %s vol0 playing=%lu", 211, g_vcFile[211], g_vcH ? pBASS_ChannelIsActive(g_vcH) : 0); while (g_vcH && pBASS_ChannelIsActive(g_vcH)) Sleep(50); vlog("selftest done");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID _) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        char *s = strrchr(g_dir, '\\'); if (s) *s = 0;
        snprintf(g_logpath, sizeof g_logpath, "%s\\VCMusic.log", g_dir);
        DeleteFileA(g_logpath);
        InitializeCriticalSection(&g_cs);
        init();
    }
    return TRUE;
}
