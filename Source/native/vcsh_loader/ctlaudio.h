/* ctlaudio.h - DualSense controller audio for the GTA IV DualSense mod (step 169, Claude 2026-11-03).
 *
 * Only the mod's own sound files go to the controller; the game's audio is never touched. GTA_IV_DualSense.asi
 * (VERIFIED FILE, build 03bae1ad / 65bc890f) runs its own XAudio2 engine: 0x10010780 picks the render endpoint named
 * "dualsense"/"wireless controller", 0x1000fed0 creates the mastering voice on it, and every sound's source voice gets
 * SetOutputMatrix (vtable +0x40) sending it only to channels 3/4 (RL/RR = HD voice coils).
 *
 * 1. Speaker: SetOutputMatrix of the mod's source voices is wrapped; when the destination has 4 channels, FL/FR (the
 *    controller speaker) get the same sound as RL/RR, scaled by [ControllerAudio] SpeakerVolume. Works for a USB pad,
 *    the Linux Bluetooth sink (step 166) and the in-game Bluetooth path below.
 * 2. Bluetooth (no DualSense audio device exists): if the mod finds no endpoint, the finder returns the virtual id
 *    "VCDS5BT"; CreateMasteringVoice on that id opens a 4-channel mastering voice on the default device with an XAPO tap
 *    that copies the mod's mix and outputs silence. A sender thread turns it into DualSense Bluetooth report 0x36 every
 *    512 frames (10.667 ms): sub-packet 0x12 = 64 int8 haptic samples (RL/RR, low-pass, 6 kHz), 0x13 = 200-byte Opus CBR
 *    speaker frame (FL/FR), 0x10 = state snapshot that never touches triggers, rumble, motor power or lightbar.
 *    Protocol from LinuxAudio4Dualsense5 0.6 (AGPL-3.0, user-tested speaker tone 2026-11-03). The virtual id is offered
 *    only after a successful first write to the pad, so a failure leaves the mod on rumble motors as before. */
#include <objbase.h>
#include <mmsystem.h>
#include <math.h>
#include "opus.h"

/* Step 171: SpeakerVolume is live. The matrix above routes FL/FR at unity; the volume is applied where the mix leaves the
 * engine - in the Bluetooth tap (speaker samples x volume) and, for a USB pad's 4-channel device, by an in-place gain XAPO
 * on the mod's mastering voice (FL/FR x volume). So a change (menusync.h: Settings > Audio slider) affects sounds that
 * are already playing, including the mod's long-lived engine voice (SetOutputMatrix once at 0x1001011e). */

/* Step 187: per-sound speaker routing (ds5extra.h) - the mod's SubmitSourceBuffer / DestroyVoice are wrapped as well, and the
 * mod's engine + mastering voice are remembered for the siren and heartbeat voices. */
typedef HRESULT (__stdcall *XA2SubmitFn)(void *, const void *, const void *);
typedef void (__stdcall *XA2DestroyFn)(void *);
static XA2SubmitFn real_submit; static XA2DestroyFn real_destroy;
static void *volatile ca_mod_xa2, *volatile ca_mod_master;
static int sf_on;
static int sf_on_matrix(void *v, void *dst, UINT32 sc, const float *m, float *t);
static HRESULT __stdcall my_submit(void *v, const void *buf, const void *wma);
static void __stdcall my_destroy(void *v);

/* ---- settings ---------------------------------------------------------------------------------------------- */
static int ca_bt = 1, ca_speaker = 1; static volatile float ca_spk_vol = 1.0f; static float ca_hap_gain = 1.0f, ca_cutoff = 1000.f;
static void ca_set_spk_vol(int pct) { if (pct < 0) pct = 0; if (pct > 100) pct = 100; ca_spk_vol = pct / 100.f; }
static void ca_load_ini(void) {
    ca_bt = vc_profile_int("ControllerAudio", "Bluetooth", 1, g_ini);
    ca_speaker = vc_profile_int("ControllerAudio", "Speaker", 1, g_ini);
    ca_spk_vol = vc_profile_int("ControllerAudio", "SpeakerVolume", 100, g_ini) / 100.f;
    ca_hap_gain = vc_profile_int("ControllerAudio", "HapticGain", 100, g_ini) / 100.f;
    ca_cutoff = (float)vc_profile_int("ControllerAudio", "HapticCutoff", 1000, g_ini);
    if (ca_spk_vol < 0) ca_spk_vol = 0; if (ca_spk_vol > 1) ca_spk_vol = 1;   /* step 180: 0-100 % */
    if (ca_hap_gain < 0) ca_hap_gain = 0; if (ca_hap_gain > 4) ca_hap_gain = 4;
    if (ca_cutoff < 100) ca_cutoff = 100; if (ca_cutoff > 2900) ca_cutoff = 2900;
}

/* ---- ring buffer: XAudio2 thread -> sender thread (frames of {speaker, haptic}) ------------------------------- */
#define CA_RING 32768u   /* frames, power of two (~0.68 s) */
static float ca_ring[CA_RING][2]; static volatile LONG ca_w, ca_r;
static void ca_push(float spk, float hap) {
    LONG w = ca_w; if ((ULONG)(w - ca_r) >= CA_RING) return;            /* full: drop (sender stalled) */
    ca_ring[(ULONG)w & (CA_RING - 1)][0] = spk; ca_ring[(ULONG)w & (CA_RING - 1)][1] = hap;
    InterlockedExchange(&ca_w, w + 1);
}
static UINT32 ca_pop(float (*out)[2], UINT32 want) {
    LONG r = ca_r, avail = ca_w - r; UINT32 n = (UINT32)(avail < (LONG)want ? (avail < 0 ? 0 : avail) : (LONG)want);
    for (UINT32 i = 0; i < n; i++) { out[i][0] = ca_ring[(ULONG)(r + i) & (CA_RING - 1)][0]; out[i][1] = ca_ring[(ULONG)(r + i) & (CA_RING - 1)][1]; }
    InterlockedExchange(&ca_r, r + (LONG)n); return n;
}

/* ---- XAPO tap (in-place, copies and silences) ----------------------------------------------------------------- */
typedef struct { GUID clsid; WCHAR FriendlyName[256]; WCHAR CopyrightInfo[256]; UINT32 MajorVersion, MinorVersion, Flags,
                 MinInputBufferCount, MaxInputBufferCount, MinOutputBufferCount, MaxOutputBufferCount; } CaRegProps;
typedef struct { const WAVEFORMATEX *pFormat; UINT32 MaxFrameCount; } CaLockParams;
typedef struct { void *pBuffer; int BufferFlags; UINT32 ValidFrameCount; } CaProcParams;   /* 0 silent, 1 valid */
typedef struct { void *pEffect; BOOL InitialState; UINT32 OutputChannels; } CaEffectDesc;
typedef struct { UINT32 EffectCount; CaEffectDesc *pEffectDescriptors; } CaEffectChain;
static const GUID CA_IID_IUnknown = {0x00000000,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
static const GUID CA_IID_IXAPO    = {0xa410b984,0x9839,0x4819,{0xa0,0xbe,0x28,0x56,0xae,0x6b,0x3a,0xdb}};
static const GUID CA_CLSID_TAP    = {0x5c1d5b70,0x6a7e,0x4a3c,{0x9d,0x16,0x56,0x43,0x44,0x35,0x42,0x54}};
typedef struct { void **vt; } CaTap;
static CaTap ca_tap; static volatile LONG ca_tap_on; static UINT32 ca_tap_ch = 4;
static HRESULT __stdcall t_qi(CaTap *t, const GUID *iid, void **pp) {
    if (!pp) return E_POINTER;
    if (IsEqualGUID(iid, &CA_IID_IUnknown) || IsEqualGUID(iid, &CA_IID_IXAPO)) { *pp = t; return S_OK; }
    *pp = NULL; return E_NOINTERFACE;
}
static ULONG __stdcall t_addref(CaTap *t) { (void)t; return 2; }
static ULONG __stdcall t_release(CaTap *t) { (void)t; return 1; }
static HRESULT __stdcall t_regprops(CaTap *t, CaRegProps **pp) {
    (void)t; CaRegProps *p = CoTaskMemAlloc(sizeof *p); if (!p) return E_OUTOFMEMORY; memset(p, 0, sizeof *p);
    p->clsid = CA_CLSID_TAP; lstrcpyW(p->FriendlyName, L"VC DualSense tap"); p->MajorVersion = 1;
    p->Flags = 0x3F;   /* channels/framerate/bits/buffercount must match, in-place supported + required */
    p->MinInputBufferCount = p->MaxInputBufferCount = p->MinOutputBufferCount = p->MaxOutputBufferCount = 1;
    *pp = p; return S_OK;
}
static HRESULT __stdcall t_fmt(CaTap *t, const WAVEFORMATEX *a, const WAVEFORMATEX *b, WAVEFORMATEX **pp) {
    (void)t; (void)a; (void)b; if (pp) *pp = NULL; return S_OK;
}
static HRESULT __stdcall t_init(CaTap *t, const void *d, UINT32 n) { (void)t; (void)d; (void)n; return S_OK; }
static void __stdcall t_reset(CaTap *t) { (void)t; }
static HRESULT __stdcall t_lock(CaTap *t, UINT32 ni, const CaLockParams *in, UINT32 no, const CaLockParams *out) {
    (void)t; (void)no; (void)out;
    if (ni && in && in[0].pFormat) ca_tap_ch = in[0].pFormat->nChannels;
    InterlockedExchange(&ca_tap_on, 1); llog("controller audio: tap active (%u channels)", ca_tap_ch); return S_OK;
}
static void __stdcall t_unlock(CaTap *t) { (void)t; InterlockedExchange(&ca_tap_on, 0); llog("controller audio: tap stopped"); }
static void __stdcall t_process(CaTap *t, UINT32 ni, const CaProcParams *in, UINT32 no, CaProcParams *out, BOOL en) {
    (void)t; (void)en;
    if (!ni || !in) return;
    UINT32 n = in[0].ValidFrameCount, ch = ca_tap_ch ? ca_tap_ch : 1; float *b = (float *)in[0].pBuffer;
    if (b && in[0].BufferFlags == 1) {
        const float vol = ca_spk_vol;
        for (UINT32 i = 0; i < n; i++) {
            const float *f = b + i * ch;
            float spk = (ch >= 2 ? 0.5f * (f[0] + f[1]) : f[0]) * vol;
            float hap = ch >= 4 ? 0.5f * (f[2] + f[3]) : (ch == 3 ? f[2] : 0.f);
            ca_push(spk, hap);
        }
        memset(b, 0, (size_t)n * ch * sizeof(float));
    } else for (UINT32 i = 0; i < n; i++) ca_push(0.f, 0.f);
    if (no && out) { out[0].BufferFlags = 0; out[0].ValidFrameCount = n; }
}
static UINT32 __stdcall t_calc(CaTap *t, UINT32 n) { (void)t; return n; }
static void *ca_tap_vt[] = { t_qi, t_addref, t_release, t_regprops, t_fmt, t_fmt, t_init, t_reset, t_lock, t_unlock, t_process, t_calc, t_calc };

/* ---- gain XAPO (in-place): a USB pad's FL/FR = controller speaker, scaled by SpeakerVolume (step 171) ---------------- */
static const GUID CA_CLSID_GAIN = {0x5c1d5b71,0x6a7e,0x4a3c,{0x9d,0x16,0x56,0x43,0x44,0x35,0x42,0x54}};
static CaTap ca_gain; static UINT32 ca_gain_ch = 4; static volatile LONG ca_gain_blocks;
static HRESULT __stdcall g_regprops(CaTap *t, CaRegProps **pp) {
    HRESULT hr = t_regprops(t, pp);
    if (SUCCEEDED(hr) && *pp) { (*pp)->clsid = CA_CLSID_GAIN; lstrcpyW((*pp)->FriendlyName, L"VC DualSense speaker volume"); }
    return hr;
}
static HRESULT __stdcall g_lock(CaTap *t, UINT32 ni, const CaLockParams *in, UINT32 no, const CaLockParams *out) {
    (void)t; (void)no; (void)out;
    if (ni && in && in[0].pFormat) ca_gain_ch = in[0].pFormat->nChannels;
    llog("controller audio: speaker volume stage active (%u channels)", ca_gain_ch); return S_OK;
}
static void __stdcall g_unlock(CaTap *t) { (void)t; }
static void __stdcall g_process(CaTap *t, UINT32 ni, const CaProcParams *in, UINT32 no, CaProcParams *out, BOOL en) {
    (void)t; (void)en;
    if (!ni || !in) return;
    UINT32 n = in[0].ValidFrameCount, ch = ca_gain_ch; float *b = (float *)in[0].pBuffer; const float vol = ca_spk_vol;
    if (b && in[0].BufferFlags == 1 && ch >= 2 && vol != 1.0f) {
        for (UINT32 i = 0; i < n; i++) { b[i * ch] *= vol; b[i * ch + 1] *= vol; }
        InterlockedIncrement(&ca_gain_blocks);
    }
    if (no && out) { out[0].BufferFlags = in[0].BufferFlags; out[0].ValidFrameCount = n; }
}
static void *ca_gain_vt[] = { t_qi, t_addref, t_release, g_regprops, t_fmt, t_fmt, t_init, t_reset, g_lock, g_unlock, g_process, t_calc, t_calc };

/* ---- XAudio2 wrappers for the mod --------------------------------------------------------------------------- */
typedef HRESULT (__stdcall *XA2Create)(void **, UINT32, UINT32);
typedef HRESULT (__stdcall *XA2CreateV)(void **, UINT32, UINT32, DWORD);
typedef HRESULT (__stdcall *XA2SrcFn)(void *, void **, const WAVEFORMATEX *, UINT32, float, void *, const void *, const void *);
typedef HRESULT (__stdcall *XA2MasterFn)(void *, void **, UINT32, UINT32, UINT32, LPCWSTR, const void *, int);
typedef HRESULT (__stdcall *XA2MatrixFn)(void *, void *, UINT32, UINT32, const float *, UINT32);
typedef HRESULT (__stdcall *XA2ChainFn)(void *, const CaEffectChain *);
static XA2Create real_xa2c; static XA2CreateV real_xa2cv;
static XA2SrcFn real_src; static XA2MasterFn real_master; static XA2MatrixFn real_matrix;
static void *ca_xa2_vt[13], *ca_src_vt[29], **ca_xa2_real_vt, **ca_src_real_vt;
static const WCHAR CA_VID[] = L"VCDS5BT";
static volatile LONG ca_bt_ready, ca_logged_matrix, ca_sent, ca_failed;

static HRESULT __stdcall my_matrix(void *v, void *dst, UINT32 sc, UINT32 dc, const float *m, UINT32 op) {
    if (ca_speaker && dc == 4 && m && sc >= 1 && sc <= 8) {
        float t[32];
        if (sf_on && sf_on_matrix(v, dst, sc, m, t)) {   /* step 187: speaker rows chosen per sound at SubmitSourceBuffer */
            if (!InterlockedExchange(&ca_logged_matrix, 1)) llog("controller audio: speaker filter routing the mod's sounds (volume %d%%, live)", (int)(ca_spk_vol * 100));
            return real_matrix(v, dst, sc, dc, t, op);
        }
        memcpy(t, m, sizeof(float) * sc * dc); int changed = 0;
        for (UINT32 d = 0; d < 2; d++) {
            int empty = 1; for (UINT32 s = 0; s < sc; s++) if (t[d * sc + s] != 0.f) empty = 0;
            if (empty) for (UINT32 s = 0; s < sc; s++) { t[d * sc + s] = t[(d + 2) * sc + s]; changed = 1; }   /* unity; volume at the output */
        }
        if (changed && !InterlockedExchange(&ca_logged_matrix, 1)) llog("controller audio: mod sounds also routed to the speaker (volume %d%%, live)", (int)(ca_spk_vol * 100));
        return real_matrix(v, dst, sc, dc, t, op);
    }
    return real_matrix(v, dst, sc, dc, m, op);
}
static HRESULT __stdcall my_src(void *x, void **pv, const WAVEFORMATEX *f, UINT32 fl, float r, void *cb, const void *sl, const void *ec) {
    HRESULT hr = real_src(x, pv, f, fl, r, cb, sl, ec);
    if (SUCCEEDED(hr) && pv && *pv) {
        void ***o = (void ***)*pv;
        if (!ca_src_real_vt) {
            ca_src_real_vt = *o; memcpy(ca_src_vt, *o, sizeof ca_src_vt); real_matrix = (XA2MatrixFn)ca_src_vt[16]; ca_src_vt[16] = (void *)my_matrix;
            real_destroy = (XA2DestroyFn)ca_src_vt[18]; ca_src_vt[18] = (void *)my_destroy;      /* step 187 */
            real_submit = (XA2SubmitFn)ca_src_vt[21]; ca_src_vt[21] = (void *)my_submit;
        }
        if (*o == ca_src_real_vt) *o = ca_src_vt;
    }
    return hr;
}
typedef struct { UINT32 CreationFlags, ActiveFlags, InputChannels, InputSampleRate; } CaVoiceDetails;   /* XAudio2 2.8/2.9 (step 169 harness: offset 8 = 4 ch) */
static void ca_attach_gain(void *mv) {
    if (!ca_speaker || !mv) return;
    CaVoiceDetails det = {0}; ((void (__stdcall *)(void *, CaVoiceDetails *))(*(void ***)mv)[0])(mv, &det);
    if (det.InputChannels != 4) { llog("controller audio: mod device has %u channels - no speaker stage", det.InputChannels); return; }
    ca_gain.vt = ca_gain_vt; CaEffectDesc d = { &ca_gain, TRUE, 4 }; CaEffectChain c = { 1, &d };   /* stateless: reusable per engine */
    HRESULT h = ((XA2ChainFn)(*(void ***)mv)[2])(mv, &c);
    llog("controller audio: USB speaker volume stage attach 0x%08lX (4 ch, %u Hz)", h, det.InputSampleRate);
}
static HRESULT __stdcall my_master(void *x, void **pm, UINT32 ch, UINT32 rate, UINT32 fl, LPCWSTR dev, const void *ec, int cat) {
    if (!(dev && !lstrcmpW(dev, CA_VID))) {
        HRESULT hr = real_master(x, pm, ch, rate, fl, dev, ec, cat);
        if (SUCCEEDED(hr) && pm && dev && !ec) { ca_attach_gain(*pm); ca_mod_master = *pm; }   /* the mod's real DualSense device (USB) */
        return hr;
    }
    HRESULT hr = real_master(x, pm, 4, 48000, fl, NULL, NULL, cat);
    if (FAILED(hr)) { llog("controller audio: mastering voice for Bluetooth failed (0x%08lX)", hr); return hr; }
    ca_tap.vt = ca_tap_vt; CaEffectDesc d = { &ca_tap, TRUE, 4 }; CaEffectChain c = { 1, &d };
    HRESULT h2 = ((XA2ChainFn)(*(void ***)*pm)[2])(*pm, &c);
    llog("controller audio: Bluetooth engine opened (4 ch, 48 kHz); tap attach 0x%08lX", h2);
    ca_mod_master = *pm;
    return hr;
}
static void ca_wrap_xa2(void *xa) {
    void ***o = (void ***)xa; if (!o || !*o) return;
    if (!ca_xa2_real_vt) {
        ca_xa2_real_vt = *o; memcpy(ca_xa2_vt, *o, sizeof ca_xa2_vt);
        real_src = (XA2SrcFn)ca_xa2_vt[5]; real_master = (XA2MasterFn)ca_xa2_vt[7];
        ca_xa2_vt[5] = (void *)my_src; ca_xa2_vt[7] = (void *)my_master;
    }
    if (*o == ca_xa2_real_vt) { *o = ca_xa2_vt; ca_mod_xa2 = xa; llog("controller audio: mod's XAudio2 engine hooked"); }
}
static HRESULT __stdcall my_xa2c(void **pp, UINT32 f, UINT32 p) { HRESULT hr = real_xa2c(pp, f, p); if (SUCCEEDED(hr) && pp) ca_wrap_xa2(*pp); return hr; }
static HRESULT __stdcall my_xa2cv(void **pp, UINT32 f, UINT32 p, DWORD v) { HRESULT hr = real_xa2cv(pp, f, p, v); if (SUCCEEDED(hr) && pp) ca_wrap_xa2(*pp); return hr; }
typedef FARPROC (__stdcall *GpaFn)(HMODULE, LPCSTR);
static GpaFn real_gpa;
static FARPROC __stdcall my_gpa(HMODULE m, LPCSTR n) {
    FARPROC f = real_gpa(m, n);
    if (!f || (uintptr_t)n < 0x10000) return f;
    if (!strcmp(n, "XAudio2CreateWithVersionInfo")) { real_xa2cv = (XA2CreateV)f; return (FARPROC)my_xa2cv; }
    if (!strcmp(n, "XAudio2Create")) { real_xa2c = (XA2Create)f; return (FARPROC)my_xa2c; }
    return f;
}

/* ---- the mod's endpoint finder: offer the virtual Bluetooth device when no real one exists --------------------- */
typedef void *(__stdcall *FindFn)(void *);
static FindFn real_find; static volatile LONG ca_logged_vid;
static void *__stdcall my_find(void *ws) {
    void *r = real_find(ws);
    BYTE *s = (BYTE *)ws; UINT32 *size = (UINT32 *)(s + 0x10), *capa = (UINT32 *)(s + 0x14);
    if (ca_bt && ca_bt_ready && *size == 0 && *capa >= 7) {
        memcpy(s, CA_VID, sizeof CA_VID); *size = 7;
        if (!InterlockedExchange(&ca_logged_vid, 1)) llog("controller audio: no DualSense audio device - using the in-game Bluetooth path");
    }
    return r;
}
static volatile LONG ca_mod_done; static void x_hook_mod(BYTE *m);
static int ca_patch_gpa(BYTE *base) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; int n = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *id = (void *)(base + dd.VirtualAddress); dd.VirtualAddress && id->Name; id++) {
        if (_stricmp((const char *)(base + id->Name), "KERNEL32.dll") || !id->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *on = (void *)(base + id->OriginalFirstThunk), *ft = (void *)(base + id->FirstThunk);
        for (; on->u1.AddressOfData; on++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(on->u1.Ordinal)) continue;
            if (strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(base + on->u1.AddressOfData))->Name, "GetProcAddress")) continue;
            if ((void *)ft->u1.Function == (void *)my_gpa) return 1;
            real_gpa = (GpaFn)ft->u1.Function; DWORD old;
            if (VirtualProtect(&ft->u1.Function, 4, PAGE_READWRITE, &old)) { ft->u1.Function = (DWORD_PTR)my_gpa; VirtualProtect(&ft->u1.Function, 4, old, &old); n++; }
        }
    }
    return n;
}
static void ca_patch_code(BYTE *p, const BYTE *want, const BYTE *put, size_t n, const char *what) {
    if (!readable((uintptr_t)p, n)) return;
    if (!memcmp(p, put, n)) return;                       /* already done (file build or earlier) */
    if (memcmp(p, want, n)) { llog("controller audio: %s bytes differ - left alone", what); return; }
    DWORD old; VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old); memcpy(p, put, n);
    VirtualProtect(p, n, old, &old); FlushInstructionCache(GetCurrentProcess(), p, n);
}
static void ctl_hook_mod(void) {
    BYTE *m = (BYTE *)GetModuleHandleA("GTA_IV_DualSense.asi");
    if (!m || InterlockedCompareExchange(&ca_mod_done, 1, 0)) return;
    /* identity: the finder call site lea eax,[ebp-0x4c]; push eax; call 0x10010780 at RVA 0xFF3D (VERIFIED 65bc890f/03bae1ad) */
    BYTE *cs = m + 0xFF41;
    if (!bytes_eq((uintptr_t)(m + 0xFF3D), "8D 45 B4 50 E8") || (BYTE *)(cs + 5 + *(int32_t *)(cs + 1)) != m + 0x10780) {
        llog("controller audio: unknown GTA_IV_DualSense.asi build - not hooked"); return;
    }
    if (!ca_patch_gpa(m)) llog("controller audio: GetProcAddress import not found in the mod");
    real_find = (FindFn)(m + 0x10780);
    DWORD old; VirtualProtect(cs + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t *)(cs + 1) = (int32_t)((BYTE *)my_find - (cs + 5));
    VirtualProtect(cs + 1, 4, old, &old); FlushInstructionCache(GetCurrentProcess(), cs, 5);
    /* Bluetooth gates (step 166 file patch) - applied in memory too, so a step-165 mod build also works */
    static const BYTE nop2[2] = { 0x90, 0x90 };
    ca_patch_code(m + 0x13C74, (const BYTE *)"\x75\x73", nop2, 2, "engine gate");
    ca_patch_code(m + 0x13D22, (const BYTE *)"\x75\x04", nop2, 2, "HD flag gate");
    ca_patch_code(m + 0x19499, (const BYTE *)"\x75\x0D", nop2, 2, "HD mode gate");
    llog("controller audio: mod hooked (XAudio2, speaker routing, Bluetooth endpoint)");
    x_hook_mod(m);   /* step 187: speaker filter sample map + light bar */
}

/* ---- Bluetooth sender ----------------------------------------------------------------------------------------- */
static uint32_t ca_crc_tab[256];
static uint32_t ca_crc(const BYTE *d, size_t n) {   /* CRC-32 seeded with 0xA2 (== zlib.crc32(b"\xa2" + data)) */
    if (!ca_crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; ca_crc_tab[i] = c; }
    uint32_t c = 0xFFFFFFFFu; c = ca_crc_tab[(c ^ 0xA2) & 0xFF] ^ (c >> 8);
    for (size_t i = 0; i < n; i++) c = ca_crc_tab[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static const BYTE CA_SNAP[63] = {
    0xf0, 0xa3, 0x00, 0x00, 0x7f, 0x64, 0x00, 0x09, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0x0a, 0x04,0,0,0,0x01, 0x00, 0x00,0x00,0xff };
static int ca_write(HANDLE h, const BYTE *r, DWORD n, DWORD outlen) {
    BYTE buf[1024]; DWORD len = outlen > n && outlen <= sizeof buf ? outlen : n, w = 0;
    memset(buf, 0, len); memcpy(buf, r, n);
    return WriteFile(h, buf, len, &w, NULL) ? 1 : 0;
}
static int ca_setup(HANDLE h, DWORD outlen) {
    BYTE r[78]; memset(r, 0, sizeof r);
    r[0] = 0x31; r[1] = 0x10; r[3] = 0xA0; r[4] = 0x80; r[8] = 0x64; r[10] = 0x30; r[40] = 0x02;
    uint32_t c = ca_crc(r, 74); memcpy(r + 74, &c, 4);
    return ca_write(h, r, 78, outlen);
}
static DWORD WINAPI ca_thread(LPVOID _) {
    (void)_; timeBeginPeriod(1);
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    int err = 0; OpusEncoder *enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
    if (!enc) { llog("controller audio: Opus encoder failed (%d) - Bluetooth path off", err); return 0; }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(160000)); opus_encoder_ctl(enc, OPUS_SET_VBR(0)); opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(0));
    double b0, b1, b2, a1, a2; {
        double w0 = 2.0 * 3.14159265358979 * ca_cutoff / 48000.0, cw = cos(w0), sw = sin(w0), al = sw / (2.0 * 0.7071), a0 = 1 + al;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = (1 - cw) / 2 / a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0; }
    for (int logged = 0;;) {
        char path[1024]; HANDLE h = v_open_ex(path, sizeof path, GENERIC_READ | GENERIC_WRITE);
        if (h == INVALID_HANDLE_VALUE) { InterlockedExchange(&ca_bt_ready, 0); Sleep(2000); continue; }
        PHIDP_PREPARSED_DATA pp; HIDP_CAPS caps; DWORD outlen = 0;
        if (HidD_GetPreparsedData(h, &pp)) { if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) outlen = caps.OutputReportByteLength; HidD_FreePreparsedData(pp); }
        if (outlen < 78) {   /* USB pad: it has a real audio device; the speaker routing above is all that's needed */
            if (!logged++) llog("controller audio: USB DualSense (output report %lu) - uses its own audio device", outlen);
            CloseHandle(h); Sleep(3000); continue;
        }
        if (!ca_setup(h, outlen)) { if (!logged++) llog("controller audio: Bluetooth write failed (%lu) - rumble motors stay in use", GetLastError()); CloseHandle(h); Sleep(3000); continue; }
        llog("controller audio: Bluetooth DualSense ready (output report %lu bytes)", outlen); logged = 0;
        InterlockedExchange(&ca_bt_ready, 1);
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, dx = 0, dy = 0; int fade = 0, errors = 0, quiet = 0; BYTE seq = 0, ctr = 0;
        LARGE_INTEGER next; QueryPerformanceCounter(&next);
        const LONGLONG step = fq.QuadPart * 512 / 48000;
        for (;;) {
            next.QuadPart += step; LARGE_INTEGER now;
            for (;;) { QueryPerformanceCounter(&now); LONGLONG left = next.QuadPart - now.QuadPart; if (left <= 0) break; if (left * 1000 / fq.QuadPart > 1) Sleep(1); }
            if (now.QuadPart - next.QuadPart > step * 8) next = now;   /* resync after a stall */
            float blk[512][2]; memset(blk, 0, sizeof blk);
            if (!ca_tap_on) { ca_pop(blk, 512); quiet = 1000; fade = 0; continue; }
            UINT32 got = ca_pop(blk, 512); (void)got;
            float peak = 0; for (int i = 0; i < 512; i++) { float a = fabsf(blk[i][0]) + fabsf(blk[i][1]); if (a > peak) peak = a; }
            quiet = peak < 1e-4f ? quiet + 1 : 0;
            if (quiet > 190) continue;                                     /* ~2 s of silence: stop sending until sound returns */
            BYTE pkt[398]; memset(pkt, 0, sizeof pkt);
            pkt[0] = 0x36; pkt[1] = (BYTE)((seq & 0x0F) << 4);
            pkt[2] = 0x11 | 0x80; pkt[3] = 7; pkt[4] = 0xFE; pkt[5] = pkt[6] = pkt[7] = pkt[8] = pkt[9] = 64; pkt[10] = ctr;
            pkt[11] = 0x10 | 0x80; pkt[12] = 63; memcpy(pkt + 13, CA_SNAP, 63);
            pkt[76] = 0x12 | 0x80; pkt[77] = 64;
            for (int i = 0; i < 64; i++) {                                 /* 48 kHz -> 6 kHz haptics: low-pass, DC block, int8 */
                double last = 0;
                for (int j = 0; j < 8; j++) {
                    double x = blk[i * 8 + j][1], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                    x2 = x1; x1 = x; y2 = y1; y1 = y; last = y;
                }
                double d = last - dx + 0.995 * dy; dx = last; dy = d;
                int v = (int)lrint(d * ca_hap_gain * 127.0); if (v > 127) v = 127; if (v < -127) v = -127;
                pkt[78 + i] = (BYTE)(v & 0xFF);
            }
            float pcm[480 * 2];                                            /* 512 -> 480 linear resample, mono -> both channels */
            for (int i = 0; i < 480; i++) {
                double src = i * (511.0 / 479.0); int a = (int)src, b = a < 511 ? a + 1 : a; float fr = (float)(src - a);
                float v = blk[a][0] + (blk[b][0] - blk[a][0]) * fr;
                if (fade < 1920) { v *= (float)(fade + i < 1920 ? fade + i : 1920) / 1920.f; }
                if (v > 1.f) v = 1.f; if (v < -1.f) v = -1.f;
                pcm[i * 2] = pcm[i * 2 + 1] = v;
            }
            if (fade < 1920) fade += 480;
            unsigned char op[200]; int n = opus_encode_float(enc, pcm, 480, op, 200); if (n < 0) n = 0;
            pkt[142] = 0x13 | 0x80; pkt[143] = 200; memcpy(pkt + 144, op, (size_t)n);
            uint32_t c = ca_crc(pkt, 394); memcpy(pkt + 394, &c, 4);
            if (ca_write(h, pkt, sizeof pkt, outlen)) { errors = 0; InterlockedIncrement(&ca_sent); }
            else { InterlockedIncrement(&ca_failed); if (++errors > 50) break; }
            seq = (BYTE)((seq + 1) & 0x0F); ctr++;
        }
        llog("controller audio: Bluetooth writes failing - controller gone; waiting"); InterlockedExchange(&ca_bt_ready, 0);
        CloseHandle(h); Sleep(1000);
    }
    return 0;
}
static void x_init(void);
static void ctl_init(void) {
    ca_load_ini(); x_init();
    ctl_hook_mod();
    if (ca_bt) { HANDLE h = CreateThread(NULL, 0, ca_thread, NULL, 0, NULL); if (h) { SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL); CloseHandle(h); } }
    llog("controller audio: Bluetooth=%d Speaker=%d SpeakerVolume=%d%% HapticGain=%d%%", ca_bt, ca_speaker, (int)(ca_spk_vol * 100), (int)(ca_hap_gain * 100));
}
