// The on-screen notice: what is said and when. Included at the end of DLSS-IV.cpp and only named at its top, since a
// declaration between the hot statics can reorder them in a release. transport/notice_pass.h makes and draws the picture.
// For the player who opens no file, and never in the way: eight cases show (the mod could not start or stopped; hdr=1 and
// HDR not on for any reason but Windows HDR being off; DLSS missing on NVIDIA in automatic mode for a mendable cause; the
// render size's buffer not made; another mod took the Direct3D hook; two copies of the .asi; the start-up device-thread
// check; a cfg edit during play that needs a restart). Nothing shows when all is well: one test a frame (NoticeFrame).
// IvtWarn hands its line to NoticeFromWarn, which knows the eight by how their lines begin; the screen gets one short
// sentence and the file's name. tools/test-notice-rules holds every warning the source can write against this table.
// When: a second and a half after the game's world is on screen, read from the world matrices uploaded this frame
// (m4bWorldSerial, counted in hkSetVsConstF): a world frame has hundreds to thousands, a loading screen or menu a few.
// Dead hooks: 20 seconds after the first frame (at once if they saw the world). Hooks not armed yet: two minutes at most.
// Ten seconds each (a failed start 15) with a short fade; four at most in a row, then "and N more". Each text once a session.
// Drawn with Windows' own text on a thread of its own; the frame uploads the picture once and draws one blended rectangle at
// the very end of the frame, before the HDR encode. Off under Wine and with notice=0 in the cfg (unlisted). When the picture
// cannot be made: no notice, said once in the log.
#include "shaders/ivt_notice_ps.h"
#include "transport/notice_pass.h"
#pragma comment(lib, "gdi32.lib")   // the text is Windows' own (NoticeRaster); the game has gdi32 loaded already

// ---- NOTICE RULES START (tools/test-notice-rules cuts from here to NOTICE RULES END: no Direct3D in between) ----
#ifndef NTC_NOW
#define NTC_NOW() GetTickCount64()
#endif
enum { NTC_QUEUE = 4, NTC_L1 = 128, NTC_L2 = 96, NTC_SEEN = 16 };
enum { NTC_SECONDS = 10, NTC_SECONDS_FATAL = 15, NTC_FADE_IN_MS = 250, NTC_FADE_OUT_MS = 400, NTC_SETTLE_MS = 1500, NTC_BLIND_MS = 20000,
       NTC_UNARMED_MS = 120000, NTC_WORLD_UPLOADS = 200 };     // world matrices in one frame from which the frame is the world's (a street has over a thousand)
static const char kNtcMoreWarn[]  = "More in DLSS-IV-WARNINGS.txt, next to GTAIV.exe";
static const char kNtcMoreFatal[] = "The reason is in DLSS-IV-DID-NOT-START.txt, next to GTAIV.exe";
struct NtcItem { char l1[NTC_L1]; char l2[NTC_L2]; int seconds; };

// The queue: any thread posts (device thread, relay, live cfg reader), the frame takes. A text is posted once a session
// unless `again`; a text already waiting is not queued twice.
static SRWLOCK       ntcLock = SRWLOCK_INIT;
static NtcItem       ntcQ[NTC_QUEUE];
static int           ntcQn = 0, ntcMore = 0;
static char          ntcSeen[NTC_SEEN][NTC_L1];
static int           ntcSeenN = 0;
static volatile LONG ntcPending = 0;        // something waits: the one thing a frame reads without the lock
static void NoticePost(const char* l1, const char* l2, int seconds, bool again)
{
    if (!l1 || !l1[0]) return;
    AcquireSRWLockExclusive(&ntcLock);
    bool seen = false, waiting = false;
    for (int i = 0; i < ntcSeenN; i++) if (!strcmp(ntcSeen[i], l1)) seen = true;
    for (int i = 0; i < ntcQn; i++)    if (!strcmp(ntcQ[i].l1, l1)) waiting = true;
    if (!seen && ntcSeenN < NTC_SEEN) strncpy_s(ntcSeen[ntcSeenN++], NTC_L1, l1, _TRUNCATE);
    if ((!seen || again) && !waiting)
    {
        if (ntcQn < NTC_QUEUE)
        {
            NtcItem* const q = &ntcQ[ntcQn++];
            strncpy_s(q->l1, NTC_L1, l1, _TRUNCATE);
            strncpy_s(q->l2, NTC_L2, l2 ? l2 : "", _TRUNCATE);
            q->seconds = seconds;
        }
        else ntcMore++;
        InterlockedExchange(&ntcPending, 1);
    }
    ReleaseSRWLockExclusive(&ntcLock);
}
// The frame's side: the next notice, or false when nothing waits. After the fourth come the uncounted ones as one line.
static bool NoticeTake(NtcItem* out)
{
    bool got = false;
    AcquireSRWLockExclusive(&ntcLock);
    if (ntcQn > 0)
    {
        *out = ntcQ[0];
        for (int i = 1; i < ntcQn; i++) ntcQ[i - 1] = ntcQ[i];
        ntcQn--; got = true;
    }
    else if (ntcMore > 0)
    {
        _snprintf_s(out->l1, NTC_L1, _TRUNCATE, "DLSS-IV: and %d more.", ntcMore);
        strncpy_s(out->l2, NTC_L2, kNtcMoreWarn, _TRUNCATE);
        out->seconds = NTC_SECONDS; ntcMore = 0; got = true;
    }
    if (ntcQn == 0 && ntcMore == 0 && !got) InterlockedExchange(&ntcPending, 0);
    ReleaseSRWLockExclusive(&ntcLock);
    return got;
}
static void NoticeDropAll()
{
    AcquireSRWLockExclusive(&ntcLock);
    ntcQn = 0; ntcMore = 0; InterlockedExchange(&ntcPending, 0);
    ReleaseSRWLockExclusive(&ntcLock);
}

// A warning's line and the notice it is (or none): by how the line begins, and for three of them by what it holds.
static bool NtcStarts(const char* s, const char* with) { return strncmp(s, with, strlen(with)) == 0; }
static void NoticeFromWarn(const char* line)
{
    const char* l1 = nullptr; bool again = false;
    char buf[NTC_L1];
    if (NtcStarts(line, "hdr=1 is set, but HDR is not on (decided at "))
    {
        // "Windows HDR is off" is the player's own switch for an SDR session: no notice. Fullscreen is the common other
        // cause and has a remedy; every other reason gets the plain sentence.
        if (strstr(line, "Windows HDR is off")) return;
        l1 = strstr(line, "exclusive fullscreen") ? "DLSS-IV: HDR is not on. Choose Borderless in the game's graphics options."
                                                   : "DLSS-IV: HDR is not on this session.";
    }
    else if (NtcStarts(line, "hdr=1 is set, but the back buffer came back as ")
          || NtcStarts(line, "hdr=1 is set and Windows HDR is on, but the display surface does not accept")
          || NtcStarts(line, "hdr=1: HDR engaged, but after "))
        l1 = "DLSS-IV: HDR is not on this session.";
    else if (NtcStarts(line, "upscaler=-1 (automatic) is running FSR, not DLSS: "))
    {
        // only a cause that can be mended: the driver, or the DLSS file gone. A card that cannot run DLSS gets FSR and no word.
        if (strstr(line, "NVIDIA driver is older"))            l1 = "DLSS-IV: DLSS could not start, so FSR is running. Update the NVIDIA driver.";
        else if (strstr(line, "nvngx_dlss.dll is not there"))  l1 = "DLSS-IV: DLSS could not start, so FSR is running. Copy the mod's DLSSIV folder again.";
        else return;
    }
    else if (strstr(line, " scene buffer the render scale needs could not be created"))
    {
        // the warning names the key that asked for the render size (quality or custom): the notice names the same one
        const char* const eq = strchr(line, '=');
        const int n = eq ? (int)(eq - line) : 0;
        if (n > 0 && n < 24) _snprintf_s(buf, sizeof(buf), _TRUNCATE, "DLSS-IV: The picture is wrong. Set %.*s=0 in DLSS-IV.cfg.", n, line);
        else strncpy_s(buf, sizeof(buf), "DLSS-IV: The picture is wrong. Set quality=0 in DLSS-IV.cfg.", _TRUNCATE);
        l1 = buf;
    }
    else if (NtcStarts(line, "Another mod (") && strstr(line, "replaced DLSS-IV's Direct3D hook as the game started"))
        l1 = "DLSS-IV: Another mod got in the way at start. Change the resolution once, or restart the game.";
    else if (NtcStarts(line, "DXVK could not end a state block recording"))
        l1 = "DLSS-IV: The game may render wrongly. Set threaded=0 in DLSS-IV.cfg.";
    else if (NtcStarts(line, "DLSS-IV.cfg was edited while the game runs: ") && strstr(line, "needs a relaunch"))
    { l1 = "That change in DLSS-IV.cfg needs a restart of the game."; again = true; }
    if (l1) NoticePost(l1, kNtcMoreWarn, NTC_SECONDS, again);
}
// The mod could not start, or stopped (IvtFatalNote, once a session). `elsewhere`: the game's folder refused the note.
static void NoticeFatal(bool stopped, bool elsewhere)
{
    NoticePost(stopped ? "DLSS-IV stopped, so the game now runs without it." : "DLSS-IV could not start, so the game runs without it.",
               elsewhere ? "The reason is in DLSS-IV-DID-NOT-START.txt, in the TEMP folder" : kNtcMoreFatal, NTC_SECONDS_FATAL, false);
}
// How much of the notice shows, 0 to 1, `t` milliseconds after it began: a short fade in, the hold, a short fade out.
static float NoticeShown(ULONGLONG t, int seconds)
{
    const ULONGLONG total = (ULONGLONG)seconds * 1000u;
    if (t >= total) return 0.0f;
    if (t < NTC_FADE_IN_MS) return (float)t / (float)NTC_FADE_IN_MS;
    if (t > total - NTC_FADE_OUT_MS) return (float)(total - t) / (float)NTC_FADE_OUT_MS;
    return 1.0f;
}
// Two copies of the .asi: the second installs nothing and cannot draw, and the first does not know of it, so the first makes
// an event named by the process and waits on it from Windows' pool, and the second sets it.
static HANDLE ntcTwinEv = nullptr, ntcTwinWait = nullptr;
static void NtcTwinName(char* name, size_t cap) { _snprintf_s(name, cap, _TRUNCATE, "DLSS-IV-second-copy-%lu", (unsigned long)GetCurrentProcessId()); }
static VOID CALLBACK NtcTwinSeen(PVOID, BOOLEAN timedOut)
{
    if (!timedOut) NoticePost("DLSS-IV: Two copies of DLSS-IV.asi are loaded. Remove one.", kNtcMoreWarn, NTC_SECONDS, false);
}
static void NoticeWatchSecondCopy()        // the first copy, once, before a second copy is let on
{
    char name[64]; NtcTwinName(name, sizeof(name));
    ntcTwinEv = CreateEventA(nullptr, TRUE, FALSE, name);
    if (ntcTwinEv) RegisterWaitForSingleObject(&ntcTwinWait, ntcTwinEv, NtcTwinSeen, nullptr, INFINITE, WT_EXECUTEONLYONCE);
}
static void NoticeTellFirstCopy()          // the second copy
{
    char name[64]; NtcTwinName(name, sizeof(name));
    const HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (ev) { SetEvent(ev); CloseHandle(ev); }
}
// notice=0 in the cfg (unlisted, for recording). The parser does not know the key, so it is among the lines it could not use;
// found there, it is taken out of that list. 0 turns the notices off for the session (a live edit can turn them off, not on).
static void NoticeCfgOverride()
{
    static const char kKey[] = "notice = \"";
    for (int i = 0; i < ivtCfgBadN; i++)
    {
        if (_strnicmp(ivtCfgBad[i], kKey, sizeof(kKey) - 1) != 0) continue;
        const char* const v = ivtCfgBad[i] + sizeof(kKey) - 1;
        if (v[0] == '0' && v[1] == '"') ntcOff = true;
        for (int j = i + 1; j < ivtCfgBadN; j++) memcpy(ivtCfgBad[j - 1], ivtCfgBad[j], sizeof(ivtCfgBad[0]));
        ivtCfgBadN--; ivtCfgBadSeen--; i--;
    }
}
// ---- NOTICE RULES END ----

// ---- The frame's side (the render thread alone) ----
// ntcOff is defined at the top of DLSS-IV.cpp: the cfg's parser stands above this file.
struct NtcJob { wchar_t l1[NTC_L1]; wchar_t l2[NTC_L2]; unsigned w, h; NoticeBitmap bm; bool ok; volatile LONG done; };
static NtcJob                 ntcJob;
static HANDLE                 ntcThread = nullptr;
static int                    ntcState = 0;         // 0 nothing shows, 1 the picture is being made, 2 it shows
static bool                   ntcDrew = false;      // a notice was drawn into the frame that has just ended
static NtcItem                ntcCur;
static IDirect3DTexture9*     ntcTex = nullptr;
static IDirect3DPixelShader9* ntcPS = nullptr;
static IDirect3DDevice9*      ntcDev = nullptr;
static ULONGLONG              ntcT0 = 0, ntcWorldAt = 0, ntcFirstAt = 0;
static unsigned               ntcBw = 0, ntcBh = 0, ntcUploads = 0;
static unsigned __int64       ntcSerial = 0;
static LONG                   ntcSerialFrame = 0;
static bool                   ntcFailSaid = false;
static DWORD WINAPI NtcRasterProc(LPVOID p)
{
    NtcJob* const j = (NtcJob*)p;
    j->ok = NoticeRaster(j->l1, j->l2[0] ? j->l2 : nullptr, j->w, j->h, &j->bm);
    InterlockedExchange(&j->done, 1);
    return 0;
}
static void NtcCannot(const char* what)
{
    if (!ntcFailSaid) { ntcFailSaid = true; LogRecord("  notice: not shown (%s); the file has it", what); }
}
static __declspec(noinline) void NoticeStep(IDirect3DDevice9* dev)
{
    const ULONGLONG now = NTC_NOW();
        // The world matrices of this frame: the tracker's count since the last call, only when that was the frame before.
    {
        const unsigned __int64 serial = m4bWorldSerial; const LONG fr = (LONG)frames;
        ntcUploads = (fr - ntcSerialFrame == 1) ? (unsigned)(serial - ntcSerial) : 0;
        ntcSerial = serial; ntcSerialFrame = fr;
    }
    if (dev != ntcDev)                        // another device: what was made on the old one went with it
    { ntcDev = dev; ntcTex = nullptr; ntcPS = nullptr; if (ntcState == 2) ntcState = 0; }
    if (ntcState == 0)
    {
        if (ntcOff || ivtWine) { NoticeDropAll(); return; }
        if (!ntcFirstAt) ntcFirstAt = now;
                // The world on screen. Hooks tracking: this frame's world matrices say it. Hooks dead: at once if the world was seen, else 20
        // seconds after the first frame. Not armed yet: wait for them, at most two minutes.
        const bool world = dead ? (m4bHaveWorld || now - ntcFirstAt >= NTC_BLIND_MS)
                         : toldVerdict != 0 ? ntcUploads >= NTC_WORLD_UPLOADS
                         : now - ntcFirstAt >= NTC_UNARMED_MS;
        if (!world) { ntcWorldAt = 0; return; }
        if (!ntcWorldAt) ntcWorldAt = now;
        if (now - ntcWorldAt < NTC_SETTLE_MS) return;
        IDirect3DSurface9* bb = nullptr; D3DSURFACE_DESC d = {};
        if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
        const bool sized = SUCCEEDED(bb->GetDesc(&d)) && d.Width >= 320 && d.Height >= 200;
        bb->Release();
        if (!sized || !NoticeTake(&ntcCur)) return;
        memset(&ntcJob, 0, sizeof(ntcJob));
        MultiByteToWideChar(CP_ACP, 0, ntcCur.l1, -1, ntcJob.l1, NTC_L1);
        MultiByteToWideChar(CP_ACP, 0, ntcCur.l2, -1, ntcJob.l2, NTC_L2);
        ntcJob.w = d.Width; ntcJob.h = d.Height;
        ntcThread = CreateThread(nullptr, 0, NtcRasterProc, &ntcJob, 0, nullptr);
        if (!ntcThread) NtcRasterProc(&ntcJob);      // no thread to be had: here, once
        ntcState = 1;
        return;
    }
    if (ntcState == 1)
    {
        if (!ntcJob.done) return;
        if (ntcThread) { CloseHandle(ntcThread); ntcThread = nullptr; }
        bool ok = ntcJob.ok && ntcJob.bm.bgra != nullptr;
        if (ok)
        {
            RsOwnCreateCall mine;                    // the mod's own: not a target for the render scale to shrink
            if (!ntcPS && (FAILED(dev->CreatePixelShader((const DWORD*)kIvtNoticePS, &ntcPS)) || !ntcPS)) { ntcPS = nullptr; ok = false; }
            ok = ok && IvtEnsureFsQuad(dev) && SUCCEEDED(NoticeUpload(dev, &ntcJob.bm, &ntcTex)) && ntcTex;
        }
        ntcBw = ntcJob.bm.w; ntcBh = ntcJob.bm.h;
        free(ntcJob.bm.bgra); ntcJob.bm.bgra = nullptr;
        if (!ok) { NtcCannot("its picture or its texture could not be made"); ntcTex = nullptr; ntcState = 0; return; }
        LogRecord("  notice: on screen for %d s (%u world matrices this frame): %s", ntcCur.seconds, ntcUploads, ntcCur.l1);
        ntcT0 = now; ntcState = 2;
    }
    const float show = NoticeShown(now - ntcT0, ntcCur.seconds);
    if (now - ntcT0 >= (ULONGLONG)ntcCur.seconds * 1000u)     // over: the next one, if any, from the next frame
    {
        if (ntcTex) { ntcTex->Release(); ntcTex = nullptr; }
        ntcState = 0;
        return;
    }
    IDirect3DSurface9* bb = nullptr; D3DSURFACE_DESC d = {};
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    if (FAILED(bb->GetDesc(&d)) || d.Width != ntcJob.w || d.Height != ntcJob.h)      // the screen changed size under it: this notice ends
    { bb->Release(); if (ntcTex) { ntcTex->Release(); ntcTex = nullptr; } ntcState = 0; return; }
    {
        RsOwnCall rsOwnGuard;                        // the mod's own binds are not the game's state
        IvtSavedState o;
        IvtSaveState(dev, &o);
        const float c[9][4] = {};
        ivtInOwnDraw = true;
        IvtSetupPass(dev, &o, c);
        DWORD oldSrgb = 0;                           // plain values: no sRGB read
        dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldSrgb);
        dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        if (FAILED(NoticeDraw(dev, bb, ntcTex, ntcPS, d.Width, d.Height, ntcBw, ntcBh, show))) NtcCannot("its draw was refused");
        else ntcDrew = true;
        dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldSrgb);
        ivtInOwnDraw = false;
        IvtRestoreState(dev, &o);
    }
    bb->Release();
}
// Every frame, at its very end (hkEndScene, before the HDR encode). With nothing waiting or showing: two loads and out.
static void NoticeFrame(IDirect3DDevice9* dev)
{
    ntcDrew = false;
    if (!ntcPending && ntcState == 0) return;
    NoticeStep(dev);
}
// For frame generation's hand-over: on a frame with no game interface a notice is handed on as scene and would flicker in
// generated frames. True from the notice's draw at a frame's end until the next frame's end. The release line does not
// call it; its name stands at the top of DLSS-IV.cpp with the notice's others.
static bool NoticeDrewThisFrame() { return ntcDrew; }
