/* ds5extra.h - step 187 (Claude, 2026-11-04): GTA V-style DualSense extras on top of step 169/171/180 controller audio.
 *
 * 1. Speaker filter. The DualSense mod (GTA_IV_DualSense.asi 03bae1ad, VERIFIED FILE) plays every haptic sample on its own
 *    XAudio2 source voice: loop slots (0x10002650: CreateSourceVoice, SetOutputMatrix, SubmitSourceBuffer, Start) and a
 *    one-shot pool keyed by format (0x1001a3b0 / 0x1001a650: voices are REUSED for different samples, SetOutputMatrix only
 *    when a voice is created). So the speaker rows are decided per sound at SubmitSourceBuffer (vtable slot 21): the buffer's
 *    pAudioData is the sample's data vector, found in the mod's sample cache std::map<std::wstring, Sample> at 0x1005f4d4
 *    (head, size +4; node: isnil +0xd, key wstring +0x10, Sample +0x28 = {begin, end, cap, WAVEFORMATEX}). Both callers hold
 *    the mod's lock 0x1005e398 while submitting, the map changes only under it. The key is the path
 *    ("audio\RESIDENT\COLLISIONS\x.wav", "audio\vehicles\BANK\x.wav", ...) -> category -> [ControllerAudio] Speaker<Category>.
 *    The haptic rows (RL/RR) are never changed. Default: every mod sound haptics only (user: "i only want the stuff i mentioned
 *    coming out the speaker" = siren, near-death heartbeat, car conversations).
 * 2. Light bar. The mod's report builder 0x100193d0 (ecx/edx = trigger effects, stack R, G, B, player-LED count; it skips the
 *    HID write when nothing changed and < 500 ms passed) is reached from 6 call sites; they now go through lb_stub, which
 *    replaces R, G, B: health 100 % green -> 50 % yellow -> 1 % red, and while wanted a police double-flash red/blue.
 * 3. Game state (game thread, VCScriptHook frame tick, every 50 ms): player health/max (health % = (hp-100)/(max-100), the
 *    mod's own formula at 0x1001b8fc: [0x10057f50] = 100.0), dead, wanted level, pause menu, nearest police (cop vehicles
 *    and cop peds by nested-box search with IS_COP_*_IN_AREA_3D_NO_SAVE).
 * 4. Siren: while wanted, the live game's SIREN_1 wail (plugins/audio/RESIDENT/HORNS, made loopable) plays on the controller
 *    speaker only, loudness from the distance of the nearest police (full at <= 5 m, silent at SirenRange).
 * 5. Heartbeat near death (user choice 2026-11-04: "Add heartbeat, TV + pad"): at <= NearDeathPercent health a heartbeat
 *    plays at the same moment on the PC/TV (own XAudio2 engine on the default device, x the game's SFX volume) and on the
 *    controller speaker; 70 bpm at the threshold up to 140 bpm at 1 %. Stops when healed, dead or paused.
 * Controller-speaker sounds (2, 4, 5) go through the existing SpeakerVolume stage (Settings > Audio slider). */

#include <wctype.h>
#include <stdlib.h>
/* ---- settings ------------------------------------------------------------------------------------------------- */
enum { SC_ENGINE, SC_ROAD, SC_IMPACT, SC_WEAPON, SC_EXPLOSION, SC_AMBIENT, SC_OTHER, SC_N };
static const char *const sc_key[SC_N] = { "SpeakerEngine", "SpeakerRoad", "SpeakerImpacts", "SpeakerWeapons", "SpeakerExplosions", "SpeakerAmbient", "SpeakerOther" };
static const char *const sc_name[SC_N] = { "engine/vehicle", "road/tyres", "impacts", "weapons", "explosions/fire", "ambient", "other" };
static const int sc_default[SC_N] = { 0, 0, 0, 0, 0, 0, 0 };   /* user 2026-11-04: "i need weapons to get haptics only and everything else too" */
static int sf_allow[SC_N];   /* sf_on: ctlaudio.h */
static int lb_health = 1, lb_police = 1, sr_on = 1, hb_on = 1, hb_pct = 20;
static float sr_vol = 1.f, sr_range = 120.f, hb_pad_vol = 1.f, hb_tv_vol = 0.7f;
static int x_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void x_load_ini(void) {
    sf_on = vc_profile_int("ControllerAudio", "SpeakerFilter", 1, g_ini) != 0;
    for (int i = 0; i < SC_N; i++) sf_allow[i] = vc_profile_int("ControllerAudio", sc_key[i], sc_default[i], g_ini) != 0;
    sr_on = vc_profile_int("ControllerAudio", "Siren", 1, g_ini);
    sr_vol = x_clamp(vc_profile_int("ControllerAudio", "SirenVolume", 100, g_ini), 0, 100) / 100.f;
    sr_range = (float)x_clamp(vc_profile_int("ControllerAudio", "SirenRange", 120, g_ini), 20, 400);
    hb_on = vc_profile_int("ControllerAudio", "Heartbeat", 1, g_ini);
    hb_pct = x_clamp(vc_profile_int("ControllerAudio", "HeartbeatBelowPercent", 20, g_ini), 2, 60);
    hb_pad_vol = x_clamp(vc_profile_int("ControllerAudio", "HeartbeatControllerVolume", 100, g_ini), 0, 100) / 100.f;
    hb_tv_vol = x_clamp(vc_profile_int("ControllerAudio", "HeartbeatTvVolume", 70, g_ini), 0, 100) / 100.f;
    lb_health = vc_profile_int("Lightbar", "HealthColours", 1, g_ini);
    lb_police = vc_profile_int("Lightbar", "PoliceFlash", 1, g_ini);
}

/* ---- 1. speaker filter ------------------------------------------------------------------------------------------ */
static BYTE *x_mod;                                   /* GTA_IV_DualSense.asi base (relocatable: DYNAMICBASE) */
static int sf_mod_ok;                                 /* sample map + lock verified in this build */
static CRITICAL_SECTION sf_cs; static int sf_cs_ok;
#define SF_SAMPLES 8192u
static struct { const void *data; signed char cat; } sf_tab[SF_SAMPLES]; static UINT32 sf_count;
static UINT32 sf_hash(const void *p) { uintptr_t x = (uintptr_t)p; x ^= x >> 13; x *= 0x9E3779B1u; return (UINT32)(x >> 7) & (SF_SAMPLES - 1); }
static int sf_find(const void *p) {
    for (UINT32 i = sf_hash(p), n = 0; n < SF_SAMPLES; i = (i + 1) & (SF_SAMPLES - 1), n++) {
        if (sf_tab[i].data == p) return sf_tab[i].cat;
        if (!sf_tab[i].data) return -1;
    }
    return -1;
}
static void sf_put(const void *p, int cat) {
    if (!p || sf_count >= SF_SAMPLES * 3 / 4) return;
    for (UINT32 i = sf_hash(p);; i = (i + 1) & (SF_SAMPLES - 1)) {
        if (sf_tab[i].data == p) { sf_tab[i].cat = (signed char)cat; return; }
        if (!sf_tab[i].data) { sf_tab[i].data = p; sf_tab[i].cat = (signed char)cat; sf_count++; return; }
    }
}
static int sf_has(const WCHAR *s, const WCHAR *k) {   /* case-insensitive substring */
    for (; *s; s++) {
        const WCHAR *a = s, *b = k;
        while (*a && *b && towlower(*a) == towlower(*b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}
static int sf_classify(const WCHAR *n) {
    if (sf_has(n, L"\\vehicles\\") || sf_has(n, L"\\engine_resident\\") || sf_has(n, L"\\helicopter\\") || sf_has(n, L"\\train\\") ||
        sf_has(n, L"jet_whine") || sf_has(n, L"jet_engine") || sf_has(n, L"afterburner")) return SC_ENGINE;
    if (sf_has(n, L"\\wheel_loops\\")) return SC_ROAD;
    if (sf_has(n, L"\\collisions\\") || sf_has(n, L"\\melee\\") || sf_has(n, L"fall-heavy") || sf_has(n, L"fall-light") || sf_has(n, L"car_splash")) return SC_IMPACT;
    if (sf_has(n, L"\\weapons\\") || sf_has(n, L"\\vc_weapons\\") || sf_has(n, L"pistol-shot") || sf_has(n, L"shutgun") || sf_has(n, L"fast-sohts")) return SC_WEAPON;
    if (sf_has(n, L"\\explosions\\") || sf_has(n, L"explosion") || sf_has(n, L"on-fire")) return SC_EXPLOSION;
    if (sf_has(n, L"\\rain\\") || sf_has(n, L"\\water\\") || sf_has(n, L"\\amb_resident\\") || sf_has(n, L"\\feet_resident\\")) return SC_AMBIENT;
    return SC_OTHER;
}
static void sf_node(uintptr_t node) {                 /* one map node -> table */
    if (!readable(node, 0x48) || *(BYTE *)(node + 0xd)) return;
    UINT32 size = *(UINT32 *)(node + 0x20), cap = *(UINT32 *)(node + 0x24);
    const WCHAR *key = cap > 7 ? *(const WCHAR **)(node + 0x10) : (const WCHAR *)(node + 0x10);
    if (size == 0 || size > 512 || !readable((uintptr_t)key, (size + 1) * 2)) return;
    WCHAR name[520]; memcpy(name, key, size * 2); name[size] = 0;
    const void *data = *(const void **)(node + 0x28);
    if (data && sf_find(data) < 0) sf_put(data, sf_classify(name));
}
static void sf_walk(void) {                          /* caller holds the mod's lock (recursive critical section) */
    uintptr_t head = *(uintptr_t *)(x_mod + 0x5f4d4); UINT32 n = *(UINT32 *)(x_mod + 0x5f4d8);
    if (!readable(head, 0x10) || n > 100000) return;
    uintptr_t stack[128]; int sp = 0; uintptr_t root = *(uintptr_t *)(head + 4);
    if (root && root != head) stack[sp++] = root;
    for (UINT32 visited = 0; sp && visited < n + 8; visited++) {
        uintptr_t x = stack[--sp];
        if (!readable(x, 0x48) || *(BYTE *)(x + 0xd)) continue;
        sf_node(x);
        uintptr_t l = *(uintptr_t *)x, r = *(uintptr_t *)(x + 8);
        if (l && l != head && sp < 128) stack[sp++] = l;
        if (r && r != head && sp < 128) stack[sp++] = r;
    }
}
static int sf_cat_of(const void *data) {
    int c = sf_find(data); if (c >= 0) return c;
    if (sf_mod_ok) {
        CRITICAL_SECTION *lk = (CRITICAL_SECTION *)(x_mod + 0x5e398);
        EnterCriticalSection(lk); sf_walk(); LeaveCriticalSection(lk);
        c = sf_find(data);
    }
    if (c < 0) { c = SC_OTHER; sf_put(data, c); }
    return c;
}
/* per voice: destination + the mod's haptic rows, so the speaker rows can be switched per sound */
#define SF_VOICES 512
static struct { void *v, *dst; UINT32 sc; float rl[8], rr[8]; signed char allow; } sf_v[SF_VOICES];
static volatile LONG sf_logged[SC_N];
static int sf_slot(void *v, int create) {
    int fre = -1;
    for (int i = 0; i < SF_VOICES; i++) { if (sf_v[i].v == v) return i; if (!sf_v[i].v && fre < 0) fre = i; }
    if (!create) return -1;
    if (fre < 0) fre = (int)(((uintptr_t)v >> 4) % SF_VOICES);    /* table full: reuse (voice gets a fresh matrix below) */
    sf_v[fre].v = v; sf_v[fre].allow = -1; return fre;
}
static void sf_build(int i, int allow, float *t) {     /* 4 rows x sc: FL, FR, RL, RR */
    UINT32 sc = sf_v[i].sc;
    for (UINT32 s = 0; s < sc; s++) {
        t[0 * sc + s] = allow ? sf_v[i].rl[s] : 0.f; t[1 * sc + s] = allow ? sf_v[i].rr[s] : 0.f;
        t[2 * sc + s] = sf_v[i].rl[s]; t[3 * sc + s] = sf_v[i].rr[s];
    }
}
/* from my_matrix (the mod sets the haptic matrix): remember it, return the matrix to apply in t */
static int sf_on_matrix(void *v, void *dst, UINT32 sc, const float *m, float *t) {
    if (!sf_cs_ok) return 0;
    EnterCriticalSection(&sf_cs);
    int i = sf_slot(v, 1); sf_v[i].dst = dst; sf_v[i].sc = sc;
    for (UINT32 s = 0; s < sc; s++) { sf_v[i].rl[s] = m[2 * sc + s]; sf_v[i].rr[s] = m[3 * sc + s]; }
    int allow = sf_v[i].allow > 0; sf_build(i, allow, t);
    LeaveCriticalSection(&sf_cs);
    return 1;
}
static HRESULT __stdcall my_submit(void *v, const void *buf, const void *wma) {
    if (ca_speaker && sf_on && sf_cs_ok && buf && real_matrix) {
        const void *data = *(const void *const *)((const BYTE *)buf + 8);   /* XAUDIO2_BUFFER.pAudioData */
        int cat = sf_cat_of(data), allow = sf_allow[cat];
        EnterCriticalSection(&sf_cs);
        int i = sf_slot(v, 0);
        if (i >= 0 && sf_v[i].allow != allow) {
            float t[32]; sf_build(i, allow, t);
            if (SUCCEEDED(real_matrix(v, sf_v[i].dst, sf_v[i].sc, 4, t, 0))) sf_v[i].allow = (signed char)allow;
        }
        LeaveCriticalSection(&sf_cs);
        if (!InterlockedExchange(&sf_logged[cat], 1))
            llog("speaker filter: first %s sound -> %s", sc_name[cat], allow ? "controller speaker + haptics" : "haptics only");
    }
    return real_submit(v, buf, wma);
}
static void __stdcall my_destroy(void *v) {
    if (sf_cs_ok) { EnterCriticalSection(&sf_cs); int i = sf_slot(v, 0); if (i >= 0) sf_v[i].v = NULL; LeaveCriticalSection(&sf_cs); }
    real_destroy(v);
}

/* ---- 3. game state (game thread) -------------------------------------------------------------------------------- */
typedef struct { void *ret; UINT32 argc; void *args; UINT32 zero; } XNatCtx;   /* = the mod's context (0x1001b646) */
typedef void (__cdecl *XNatFn)(XNatCtx *);
typedef void *(__cdecl *XGetNative)(const char *);
enum { N_PLAYER_ID, N_PLAYER_CHAR, N_HEALTH, N_MAX_HEALTH, N_DEAD, N_WANTED, N_PAUSE, N_COORDS, N_COP_CAR, N_COP_PED, N_IN_COP_CAR, N_COUNT };
static const char *const x_nat_names[N_COUNT] = { "GET_PLAYER_ID", "GET_PLAYER_CHAR", "GET_CHAR_HEALTH", "GET_PLAYER_MAX_HEALTH",
    "IS_CHAR_DEAD", "STORE_WANTED_LEVEL", "IS_PAUSE_MENU_ACTIVE", "GET_CHAR_COORDINATES", "IS_COP_VEHICLE_IN_AREA_3D_NO_SAVE",
    "IS_COP_PED_IN_AREA_3D_NO_SAVE", "IS_CHAR_IN_ANY_POLICE_VEHICLE" };
static XNatFn x_nat[N_COUNT]; static int x_nat_ok;
static UINT32 x_call(int n, int argc, const UINT32 *a) {
    UINT32 r = 0, args[8] = {0}; for (int i = 0; i < argc && i < 8; i++) args[i] = a[i];
    XNatCtx c = { &r, (UINT32)argc, args, 0 }; x_nat[n](&c); return r;
}
static UINT32 x_f2u(float f) { UINT32 u; memcpy(&u, &f, 4); return u; }
static int x_player_ready(void) {   /* same validated chain as VCMemory (step 155) / ScriptHook.dll (step 161) */
    if (!readable(0x1036F14, 4) || !readable(0x18B6F1C, 4)) return 0;
    uintptr_t pool = *(uintptr_t *)0x18B6F1C;
    if (!pool || !readable(pool, 0x1c)) return 0;
    int index = *(int32_t *)0x1036F14;
    if (index < 0 || index > 31 || !readable(0x11A8808 + 4u * (unsigned)index, 4)) return 0;
    uintptr_t info = *(uintptr_t *)(0x11A8808 + 4u * (unsigned)index);
    if (!info || !readable(info, 0x59c)) return 0;
    uintptr_t ped = *(uintptr_t *)(info + 0x598);
    return ped && readable(ped, 0x220);
}
static int x_bind_natives(void) {
    if (x_nat_ok) return 1;
    static DWORD tried; DWORD now = GetTickCount(); if (now - tried < 2000) return 0; tried = now;
    HMODULE sh = GetModuleHandleA("ScriptHook.dll"); if (!sh) return 0;
    XGetNative gn = (XGetNative)GetProcAddress(sh, "?GetNativeAddress@Game@@SAPAXPBD@Z"); if (!gn) return 0;
    for (int i = 0; i < N_COUNT; i++) if (!x_nat[i] && !(x_nat[i] = (XNatFn)gn(x_nat_names[i]))) return 0;
    x_nat_ok = 1; llog("DualSense extras: game natives bound (%d)", N_COUNT); return 1;
}
static volatile LONG gs_tick, gs_health = 100, gs_wanted, gs_dead, gs_paused; static volatile float gs_cop_dist = 1e9f;
static float x_cop_dist(float x, float y, float z, int skip_cars) {
    /* nested boxes: the smallest half-size with a cop inside ~ distance to the nearest cop (box metric) */
    static const float R[] = { 4, 6, 9, 13, 18, 25, 35, 50, 70, 95, 130, 180, 250, 400 };
    for (unsigned k = 0; k < sizeof R / sizeof *R; k++) {
        float r = R[k], h = r < 15 ? 15 : r;
        UINT32 a[6] = { x_f2u(x - r), x_f2u(y - r), x_f2u(z - h), x_f2u(x + r), x_f2u(y + r), x_f2u(z + h) };
        if ((!skip_cars && x_call(N_COP_CAR, 6, a)) || x_call(N_COP_PED, 6, a)) return k ? (R[k - 1] + r) * 0.5f : r * 0.5f;
        if (r >= sr_range * 1.5f) break;
    }
    return 1e9f;
}
static void hb_beat(void); static void x_audio_frame(void);
static void ds5_frame(void) {   /* game thread, from vcsh_tick */
    static DWORD last, last_cop;
    DWORD now = GetTickCount(); if (now - last < 50) return; last = now;
    if (!x_player_ready() || !x_bind_natives()) { x_audio_frame(); return; }
    UINT32 id = x_call(N_PLAYER_ID, 0, NULL), ped = 0, hp = 0, w = 0; int mx = 200;
    UINT32 a2[2] = { id, (UINT32)(uintptr_t)&ped }; x_call(N_PLAYER_CHAR, 2, a2);
    if (!ped) { x_audio_frame(); return; }
    UINT32 h2[2] = { ped, (UINT32)(uintptr_t)&hp }; x_call(N_HEALTH, 2, h2);
    UINT32 m2[2] = { id, (UINT32)(uintptr_t)&mx }; x_call(N_MAX_HEALTH, 2, m2); if (mx <= 100) mx = 200;
    UINT32 d1[1] = { ped }; int dead = x_call(N_DEAD, 1, d1) != 0;
    UINT32 w2[2] = { id, (UINT32)(uintptr_t)&w }; x_call(N_WANTED, 2, w2);
    int paused = x_call(N_PAUSE, 0, NULL) != 0;
    float pct = ((float)hp - 100.f) * 100.f / (float)(mx - 100); if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    if (dead) pct = 0;
    InterlockedExchange(&gs_health, (LONG)(pct + 0.5f)); InterlockedExchange(&gs_wanted, (LONG)(w > 6 ? 6 : w));
    InterlockedExchange(&gs_dead, dead); InterlockedExchange(&gs_paused, paused);
    if (w && !dead && sr_on && now - last_cop >= 150) {
        last_cop = now; float x = 0, y = 0, z = 0;
        UINT32 c4[4] = { ped, (UINT32)(uintptr_t)&x, (UINT32)(uintptr_t)&y, (UINT32)(uintptr_t)&z }; x_call(N_COORDS, 4, c4);
        int in_cop_car = x_call(N_IN_COP_CAR, 1, d1) != 0;
        gs_cop_dist = x_cop_dist(x, y, z, in_cop_car);
    } else if (!w || dead) gs_cop_dist = 1e9f;
    InterlockedExchange(&gs_tick, (LONG)now);
    x_audio_frame();
}

/* ---- 2. light bar ----------------------------------------------------------------------------------------------- */
__attribute__((used)) uintptr_t lb_real __asm__("lb_real") = 0;
__attribute__((used)) void lb_override(UINT32 *a) __asm__("lb_override");
static volatile LONG lb_logged;
__attribute__((used)) void lb_override(UINT32 *a) {   /* a[0..2] = R, G, B (stack args of 0x100193d0), a[3] = player-LED value */
    if ((!lb_health && !lb_police) || GetTickCount() - (DWORD)gs_tick > 1500) return;   /* menus/loading: the mod decides */
    int r = -1, g = 0, b = 0;
    if (lb_police && gs_wanted > 0 && !gs_dead) {
        static const struct { DWORD until; BYTE r, g, b; } P[] = {   /* 600 ms: red, red, blue, blue (double flash) */
            { 90, 255, 0, 0 }, { 140, 0, 0, 0 }, { 230, 255, 0, 0 }, { 300, 0, 0, 0 },
            { 390, 0, 0, 255 }, { 440, 0, 0, 0 }, { 530, 0, 0, 255 }, { 600, 0, 0, 0 } };
        DWORD t = GetTickCount() % 600; unsigned k = 0; while (k < 7 && t >= P[k].until) k++;
        r = P[k].r; g = P[k].g; b = P[k].b;
    } else if (lb_health) {
        int p = (int)gs_health;                         /* 100 green, 50 yellow, 1 (and 0) red */
        if (p >= 50) { r = (100 - p) * 255 / 50; g = 255; }
        else { r = 255; g = p <= 1 ? 0 : (p - 1) * 255 / 49; }
        b = 0;
    }
    if (r < 0) return;
    a[0] = (UINT32)r; a[1] = (UINT32)g; a[2] = (UINT32)b;
    if (!InterlockedExchange(&lb_logged, 1)) llog("light bar: health colours%s active", lb_police ? " + police flash" : "");
}
__attribute__((naked)) static void lb_stub(void) {
    __asm__ volatile("pushal\n\tpushfl\n\tleal 40(%esp), %eax\n\tpushl %eax\n\tcall lb_override\n\taddl $4, %esp\n\tpopfl\n\tpopal\n\tjmp *lb_real\n");
}
static void lb_hook_mod(BYTE *m) {
    static const DWORD sites[] = { 0x13a1a, 0x13a3a, 0x13e6c, 0x14883, 0x14cba, 0x1a288 };
    BYTE *target = m + 0x193d0; int n = 0;
    if (!bytes_eq((uintptr_t)target, "55 8B EC 83 E4 F0 81 EC 08 01 00 00")) { llog("light bar: report builder bytes differ - left alone"); return; }
    for (unsigned i = 0; i < sizeof sites / sizeof *sites; i++) {   /* verify every site before patching any */
        BYTE *cs = m + sites[i];
        if (!readable((uintptr_t)cs, 5) || cs[0] != 0xE8 || cs + 5 + *(int32_t *)(cs + 1) != target) { llog("light bar: call site %lX differs - left alone", sites[i]); return; }
    }
    lb_real = (uintptr_t)target;
    for (unsigned i = 0; i < sizeof sites / sizeof *sites; i++) {
        BYTE *cs = m + sites[i]; DWORD old;
        VirtualProtect(cs + 1, 4, PAGE_EXECUTE_READWRITE, &old);
        *(int32_t *)(cs + 1) = (int32_t)((BYTE *)lb_stub - (cs + 5));
        VirtualProtect(cs + 1, 4, old, &old); FlushInstructionCache(GetCurrentProcess(), cs, 5); n++;
    }
    llog("light bar: %d report call sites hooked (health %d, police flash %d)", n, lb_health, lb_police);
}

/* ---- 4/5. siren + heartbeat voices ------------------------------------------------------------------------------ */
typedef struct { short *pcm; UINT32 frames; } XWav;
static XWav sr_wav, hb_pad_wav, hb_tv_wav;
static int x_load_wav(const char *name, XWav *w) {   /* 48 kHz mono 16-bit PCM (built by src/build.py) */
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\VCScriptHook\\%s", g_dir, name);
    FILE *f = fopen(p, "rb"); if (!f) { llog("DualSense extras: %s missing", p); return 0; }
    BYTE h[44]; size_t got = fread(h, 1, 44, f);
    if (got != 44 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVEfmt ", 8) || *(UINT16 *)(h + 20) != 1 || *(UINT16 *)(h + 22) != 1 ||
        *(UINT32 *)(h + 24) != 48000 || *(UINT16 *)(h + 34) != 16 || memcmp(h + 36, "data", 4)) { fclose(f); llog("DualSense extras: %s not 48 kHz mono 16-bit", p); return 0; }
    UINT32 bytes = *(UINT32 *)(h + 40); w->pcm = (short *)malloc(bytes); w->frames = bytes / 2;
    if (!w->pcm || fread(w->pcm, 1, bytes, f) != bytes) { fclose(f); free(w->pcm); w->pcm = NULL; return 0; }
    fclose(f); return 1;
}
typedef struct { UINT32 Flags, AudioBytes; const BYTE *pAudioData; UINT32 PlayBegin, PlayLength, LoopBegin, LoopLength, LoopCount; void *pContext; } XBuf;
typedef struct { void *pCurrentBufferContext; UINT32 BuffersQueued; UINT64 SamplesPlayed; } XState;
static const WAVEFORMATEX x_fmt = { 1 /* PCM */, 1, 48000, 96000, 2, 16, 0 };
#define VCALL(o, slot, T) ((T)(*(void ***)(o))[slot])
static void *x_pad_xa2, *sr_voice, *hb_pad_voice, *hb_tv_voice, *x_tv_xa2;
static volatile LONG x_tv_ready;
static void *x_pad_voice(void) {   /* new mono voice in the mod's engine, controller speaker only (FL/FR), no haptics */
    void *xa = ca_mod_xa2, *v = NULL; if (!xa || !ca_mod_master || !real_src || !real_matrix) return NULL;
    if (FAILED(real_src(xa, &v, &x_fmt, 0, 2.0f, NULL, NULL, NULL)) || !v) return NULL;
    CaVoiceDetails det = {0}; VCALL(ca_mod_master, 0, void (__stdcall *)(void *, CaVoiceDetails *))(ca_mod_master, &det);
    if (det.InputChannels != 4) { VCALL(v, 18, XA2DestroyFn)(v); return NULL; }
    static const float m[4] = { 1, 1, 0, 0 };
    real_matrix(v, NULL, 1, 4, m, 0);
    return v;
}
static void x_submit(void *v, const XWav *w, int loop) {
    XBuf b = { 0x40, w->frames * 2, (const BYTE *)w->pcm, 0, 0, 0, 0, loop ? 255u : 0u, NULL };
    VCALL(v, 21, XA2SubmitFn)(v, &b, NULL);
}
static UINT32 x_queued(void *v) { XState s = {0}; VCALL(v, 25, void (__stdcall *)(void *, XState *, UINT32))(v, &s, 0); return s.BuffersQueued; }
static void x_vol(void *v, float g) { VCALL(v, 12, HRESULT (__stdcall *)(void *, float, UINT32))(v, g, 0); }
static void x_start(void *v) { VCALL(v, 19, HRESULT (__stdcall *)(void *, UINT32, UINT32))(v, 0, 0); }
static void x_stop(void *v) { VCALL(v, 20, HRESULT (__stdcall *)(void *, UINT32, UINT32))(v, 0, 0); }
static DWORD WINAPI x_tv_thread(LPVOID _) {   /* own XAudio2 engine on the PC/TV default device for the heartbeat */
    (void)_; CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HMODULE xa = LoadLibraryExW(L"xaudio2_9.dll", NULL, 0x800); if (!xa) xa = LoadLibraryExW(L"xaudio2_8.dll", NULL, 0x800);
    XA2Create cr = xa ? (XA2Create)GetProcAddress(xa, "XAudio2Create") : NULL;
    XA2CreateV cv = xa ? (XA2CreateV)GetProcAddress(xa, "XAudio2CreateWithVersionInfo") : NULL;
    void *x = NULL, *mv = NULL, *v = NULL; Sleep(4000);
    if (!((cr && SUCCEEDED(cr(&x, 0, 1))) || (cv && SUCCEEDED(cv(&x, 0, 1, 0x0A000010)))) || !x) { llog("heartbeat: no XAudio2 for the PC/TV output"); return 0; }
    HRESULT hr = VCALL(x, 7, XA2MasterFn)(x, &mv, 0, 48000, 0, NULL, NULL, 6 /* GameEffects */);
    if (FAILED(hr) || FAILED(VCALL(x, 5, XA2SrcFn)(x, &v, &x_fmt, 0, 2.0f, NULL, NULL, NULL))) { llog("heartbeat: PC/TV voice failed (0x%08lX)", hr); return 0; }
    x_start(v); x_tv_xa2 = x; hb_tv_voice = v; InterlockedExchange(&x_tv_ready, 1);
    llog("heartbeat: PC/TV output ready (default device)");
    return 0;
}
static void hb_beat(void) {
    float sfx = readable(0x1160CAC, 4) ? (float)x_clamp(*(int32_t *)0x1160CAC, 0, 10) / 10.f : 1.f;   /* PREF_SFX_VOLUME 0-10 */
    if (hb_tv_voice && x_tv_ready && hb_tv_wav.pcm && !x_queued(hb_tv_voice)) { x_vol(hb_tv_voice, hb_tv_vol * sfx); x_submit(hb_tv_voice, &hb_tv_wav, 0); }
    if (hb_pad_voice && hb_pad_wav.pcm && !x_queued(hb_pad_voice)) { x_vol(hb_pad_voice, hb_pad_vol); x_submit(hb_pad_voice, &hb_pad_wav, 0); }
}
static void x_audio_frame(void) {   /* game thread, every 50 ms */
    DWORD now = GetTickCount(); int fresh = now - (DWORD)gs_tick < 400;
    if (ca_mod_xa2 && ca_mod_xa2 != x_pad_xa2 && ca_mod_master) {   /* (re)create the controller voices in the current mod engine */
        x_pad_xa2 = ca_mod_xa2; sr_voice = hb_pad_voice = NULL;
        if (sr_on && sr_wav.pcm && (sr_voice = x_pad_voice())) { x_vol(sr_voice, 0); x_submit(sr_voice, &sr_wav, 1); }
        if (hb_on && hb_pad_wav.pcm && (hb_pad_voice = x_pad_voice())) x_start(hb_pad_voice);
        llog("DualSense extras: controller voices ready (siren %s, heartbeat %s)", sr_voice ? "yes" : "no", hb_pad_voice ? "yes" : "no");
    }
    /* siren: loudness from the nearest police distance */
    static float sr_cur; static int sr_running; static DWORD sr_quiet_since; static volatile LONG sr_logged;
    float target = 0;
    if (sr_voice && fresh && gs_wanted > 0 && !gs_dead && !gs_paused) {
        float d = gs_cop_dist, t = (sr_range - d) / (sr_range - 5.f); if (t > 1) t = 1; if (t < 0) t = 0;
        target = t * t * sr_vol;
    }
    sr_cur += (target - sr_cur) * 0.35f; if (fabsf(target - sr_cur) < 0.005f) sr_cur = target;
    if (sr_voice) {
        if (sr_cur > 0.001f) {
            if (!sr_running) { x_start(sr_voice); sr_running = 1; if (!InterlockedExchange(&sr_logged, 1)) llog("siren: wanted - playing on the controller speaker (police %.0f m)", gs_cop_dist); }
            x_vol(sr_voice, sr_cur); sr_quiet_since = now;
        } else if (sr_running) { x_vol(sr_voice, 0); if (now - sr_quiet_since > 1000) { x_stop(sr_voice); sr_running = 0; } }
    }
    /* heartbeat near death */
    static DWORD hb_next; static volatile LONG hb_logged;
    if (hb_on && fresh && !gs_dead && !gs_paused && gs_health <= hb_pct) {
        float p = (float)gs_health; if (p < 1) p = 1;
        DWORD interval = (DWORD)(430.f + (857.f - 430.f) * (p - 1.f) / ((float)hb_pct - 1.f));   /* 140 bpm at 1 % .. 70 bpm at threshold */
        if ((LONG)(now - hb_next) >= 0) { hb_beat(); hb_next = now + interval; if (!InterlockedExchange(&hb_logged, 1)) llog("heartbeat: near death (%ld %%) - PC/TV + controller", (long)gs_health); }
    } else hb_next = now;
}
static void x_init(void) {
    x_load_ini();
    InitializeCriticalSection(&sf_cs); sf_cs_ok = 1;
    if (sr_on) x_load_wav("siren_wail_loop.wav", &sr_wav);
    if (hb_on) { x_load_wav("heartbeat_controller.wav", &hb_pad_wav); x_load_wav("heartbeat_tv.wav", &hb_tv_wav);
                 HANDLE h = CreateThread(NULL, 0, x_tv_thread, NULL, 0, NULL); if (h) CloseHandle(h); }
    char on[128] = ""; for (int i = 0; i < SC_N; i++) if (sf_allow[i]) { strcat(on, *on ? ", " : ""); strcat(on, sc_name[i]); }
    llog("DualSense extras: speaker filter %s (speaker: %s), siren %d, heartbeat %d (<= %d %%), light bar health %d police %d",
         sf_on ? "on" : "off", *on ? on : "none", sr_on, hb_on, hb_pct, lb_health, lb_police);
}
/* from ctl_hook_mod (once the mod module is known) */
static void x_hook_mod(BYTE *m) {
    x_mod = m;
    /* sample map 0x1005f4d4 and lock 0x1005e398 as relocated immediates in the lookup 0x100021e0 (mov ecx,imm / push imm) */
    sf_mod_ok = readable((uintptr_t)(m + 0x224e), 6) && m[0x224e] == 0xB9 && *(uint32_t *)(m + 0x224f) == (uint32_t)(uintptr_t)(m + 0x5f4d4) &&
                m[0x2221] == 0x68 && *(uint32_t *)(m + 0x2222) == (uint32_t)(uintptr_t)(m + 0x5e398);
    if (!sf_mod_ok) llog("speaker filter: sample map not where expected - every mod sound counts as 'other'");
    lb_hook_mod(m);
}
