// devthread/dt_trk.h: the tracker on the device thread. While the layer is armed, the mover tracker, the pose store and the
// velocity batch run on its worker, from the draw and EndScene observers, on inputs the worker rebuilds from the recorded
// stream; the render thread's four call sites test one byte (trkWorkerLive) and stand aside, and take everything back the
// moment the layer disarms.
// The observers issue no device command: they run mid-replay, where a draw of the mod's would land inside the game's stream.
// Their only device calls are two Gets (viewport, cull mode) that read the state the replay just set, and the pose texture's
// LockRect, which DXVK takes from any thread. The velocity drain runs wherever the injection body runs, at inject_worker=2 on
// this worker (the DT_NOTE_INJECT observer below): it saves what it disturbs from the device, never from a hook global, and
// what it needs of the render thread travels in the command (IvtInjectCmd).
// Every input the per-draw chain reads crosses one of three ways:
//   rebuilt by an observer from the recorded command (the world, the WVP, the view inverse, the wheel, the pose shadow; the
//     stream, indices, declaration, shader, depth-stencil): the worker's own copies, filled by the same tests the render
//     thread's hooks apply, in stream order;
//   stamped into a note where the fact is true: the tracker's window (a clock), the scene level (sent as a level, not an
//     edge), and once a frame the frame note (the previous camera and its validity, the ordinal, a resend of both levels);
//   read at a drained point: the batch at the injection, and the seed at every arm, when the worker's copies are set from the
//     render thread's globals byte for byte.
// The bodies are one source, two sides: TrkRender (DLSS-IV.cpp, before hkDrawIndexed) answers with the render thread's
// globals, TrkWorker below with trkW, the same names in the same order; that list is the contract this file keeps.

// ---- the worker's copies (written by the worker only; read by the render thread inside
// ---- a drained scope, where the worker is parked) ------------------------------------
struct TrkSide
{
    float    world[16], wvp[16], viewInv[16], wheel[16];
    bool     haveWorld, haveWVP, haveViewInv, haveWheel;
    unsigned __int64 worldSerial;     // bumped with every world copy, as m4bWorldSerial is: M4dNoteDraw's memo key
    float    boneShadow[144 * 4];
    int      boneMaxReg;
    unsigned __int64 boneSerial;
    UINT_PTR vb; UINT vbOff, vbStr; UINT_PTR ib;
        // Streams 1..3 and which of them the bound declaration reads (VelPend::xMask): the worker's copies of velCurXVB, velCurXOff,
    // velCurXStr and velCurXMask, filled by the same tests, seeded at the same place.
    UINT_PTR xvb[VEL_XSTREAMS]; UINT xoff[VEL_XSTREAMS], xstr[VEL_XSTREAMS]; unsigned char xmask;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DVertexShader9*      vs;
    IDirect3DSurface9*           ds;
    bool     skinned, posT, premul;
    bool     latched;          // toldVerdict, as the last frame note said
    bool     sceneNow;         // the scene level, as the last scene note said
    float    window;
    Mat4d    camPrevVP;
    LONG     frames;
    LONG     armCount;
    bool     camPrevValid;     // camPrev.valid, as the last frame note said
    unsigned char texld0;      // the bound vertex shader's texldl count on vertex sampler 0, as velCurVSTexld0 (VelDmgNote's pre-filter)
};
static TrkSide trkW;

// The worker's own classification caches: the same tests as hkSetVertexDecl and hkSetVertexShader, on its own arrays,
// holding a reference for the same reason, with a 64-slot direct-mapped index in front of each scan (pointer bits, index+1 in
// the slot; a miss falls to the scan).
static IDirect3DVertexDeclaration9* trkWDeclPtr[VEL_DECL_CACHE];
static unsigned char                trkWDeclFlags[VEL_DECL_CACHE];   // 1 skinned, 2 posT; 8/16/32 reads stream 1/2/3, 128 reads one above 3
// The flag byte's upper bits as VelPend::xMask spells them: bits 0..2 for streams 1..3, VEL_XMASK_BEYOND unchanged.
static __forceinline unsigned char TrkXMaskOf(unsigned char flags) { return (unsigned char)(((flags >> 3) & 7u) | (flags & VEL_XMASK_BEYOND)); }
static __forceinline unsigned char TrkFlagsOfXMask(unsigned char xm) { return (unsigned char)(((xm & 7u) << 3) | (xm & VEL_XMASK_BEYOND)); }
static int                          trkWDeclN = 0;
static IDirect3DVertexShader9*      trkWVSPtr[VEL_VS_CACHE];
static bool                         trkWVSPremul[VEL_VS_CACHE];
static unsigned char                trkWVSTexld0[VEL_VS_CACHE];   // its texldl count on vertex sampler 0 (VsCountTexldl, as hkSetVertexShader's velVSTexld0)
static int                          trkWVSN = 0;
static int                          trkWDeclIdx[64], trkWVSIdx[64];
// Development builds: the binds each table was asked about, those the 64-slot index sent to the scan, and those
// fingerprinted again because the table was full.
DEVSTAT(static LONG trkWDeclAsk = 0, trkWDeclScan = 0, trkWDeclPastCap = 0, trkWVSAsk = 0, trkWVSScan = 0, trkWVSPastCap = 0;)

// The render thread's side of the notes.
static bool     trkRSceneSent = false;
static bool     trkRNotesRefusedSaid = false;

struct TrkFrameNote
{
    Mat4d    vp;
    float    window;
    LONG     frames, armCount;
    unsigned char sceneNow, latched, camPrevValid, pad;
};

// The device thread's pending mover: TrkOnDrawPre writes it and VelRecordObject reads it,
// both inside one draw observer, so the render thread's velPending block is never shared.
static bool             trkWPend = false;
static D3DPRIMITIVETYPE trkWPendType = D3DPT_TRIANGLELIST;
static INT              trkWPendBase = 0;
static UINT             trkWPendMin = 0, trkWPendNum = 0, trkWPendStart = 0, trkWPendPrim = 0;
static Mat4             trkWPendWVP, trkWPendWVP8;
static int              trkWPendBoneRow = -1;
static bool             trkWPendPremul = false;

// THE DEVICE THREAD'S SIDE: the same names as TrkRender, in the same order, answered from trkW.
struct TrkWorker
{
    static constexpr bool kMayDraw = false;     // an observer may not draw (it runs mid-replay): VelRecordObject drops, counted, where the render thread would drain at once
    static __forceinline const float*     World()        { return trkW.world; }
    static __forceinline unsigned __int64 WorldSerial()  { return trkW.worldSerial; }
    static __forceinline bool             HaveWVP()      { return trkW.haveWVP; }
    static __forceinline const float*     WVP()          { return trkW.wvp; }
    static __forceinline bool             HaveWheel()    { return trkW.haveWheel; }
    static __forceinline const float*     Wheel()        { return trkW.wheel; }
    static __forceinline const float*     BoneShadow()   { return trkW.boneShadow; }
    static __forceinline int              BoneMaxReg()   { return trkW.boneMaxReg; }
    static __forceinline unsigned __int64 BoneSerial()   { return trkW.boneSerial; }
    static __forceinline UINT_PTR         VBKey()        { return trkW.vb; }
    static __forceinline UINT_PTR         IBKey()        { return trkW.ib; }
    static __forceinline IDirect3DVertexBuffer9*      VB()   { return (IDirect3DVertexBuffer9*)trkW.vb; }
    static __forceinline UINT             VBOff()        { return trkW.vbOff; }
    static __forceinline UINT             VBStr()        { return trkW.vbStr; }
    static __forceinline IDirect3DIndexBuffer9*       IB()   { return (IDirect3DIndexBuffer9*)trkW.ib; }
    static __forceinline IDirect3DVertexDeclaration9* Decl() { return trkW.decl; }
    static __forceinline IDirect3DSurface9*           DS()   { return trkW.ds; }
    static __forceinline bool             Skinned()      { return trkW.skinned; }
    static __forceinline bool             Multi()        { return trkW.xmask != 0; }   // as the render side
    static __forceinline unsigned char    XMask()        { return trkW.xmask; }
    static __forceinline IDirect3DVertexBuffer9*      XVB(int s)  { return (IDirect3DVertexBuffer9*)trkW.xvb[s]; }
    static __forceinline UINT             XOff(int s)    { return trkW.xoff[s]; }
    static __forceinline UINT             XStr(int s)    { return trkW.xstr[s]; }
    static __forceinline bool             Premul()       { return trkW.premul; }
    static __forceinline IDirect3DVertexShader9* VS()    { return trkW.vs; }           // as the render side (VelDmgNote)
    static __forceinline unsigned char    Texld0()       { return trkW.texld0; }
    static __forceinline float            Window()       { return trkW.window; }
    static __forceinline LONG             Frames()       { return trkW.frames; }
    static __forceinline bool             CamPrevValid() { return trkW.camPrevValid; }
    static __forceinline const Mat4d&     CamPrevVP()    { return trkW.camPrevVP; }
    static __forceinline bool&             Pend()        { return trkWPend; }
    static __forceinline D3DPRIMITIVETYPE& PendType()    { return trkWPendType; }
    static __forceinline INT&              PendBase()    { return trkWPendBase; }
    static __forceinline UINT&             PendMin()     { return trkWPendMin; }
    static __forceinline UINT&             PendNum()     { return trkWPendNum; }
    static __forceinline UINT&             PendStart()   { return trkWPendStart; }
    static __forceinline UINT&             PendPrim()    { return trkWPendPrim; }
    static __forceinline Mat4&             PendWVP()     { return trkWPendWVP; }
    static __forceinline int&              PendBoneRow() { return trkWPendBoneRow; }
    static __forceinline bool&             PendPremul()  { return trkWPendPremul; }
    static __forceinline Mat4&             PendWVP8()    { return trkWPendWVP8; }
};

// ---- the observers (worker) ----
// SetVertexShaderConstantF: c[1] start, c[2] the payload's address in the ring, c[3] count. The two tests and the wheel rule
// are hkSetVsConstF's, verbatim.
// A state-block Apply is not observed here, so an Apply that restored any of the tracker's copies (world and projection, the
// bone shadow, stream, indices, declaration, vertex shader) would leave the tracker reading what it last saw set. Applies do
// arrive every frame (FusionFix's D3DX effect and sprite Ends), but those passes run after the scene, and the game sets those
// states again for each scene draw. Development builds count that claim (dt_res.h, "state blocks:" at log=2). Should the
// count ever be above zero, the worker (and the render side while disarmed) should drop the matching copies at a recorded
// Apply whose recording held those Sets; a re-read from the device would be a drain per Apply.
static void TrkObsVsConstF(const DWORD* c)
{
    const UINT   start = c[1], count = c[3];
    const float* data  = (const float*)(UINT_PTR)c[2];
    if (dead || !data) return;
    if (trkW.latched && start == 0 && count >= 4)
    {
        trkW.worldSerial++;
        memcpy(trkW.world, data, 64);
        trkW.haveWorld = true;
        if (count >= 12) { memcpy(trkW.wvp, data + 32, 64); trkW.haveWVP = true; }
        if (count >= 16) { memcpy(trkW.viewInv, data + 48, 64); trkW.haveViewInv = true; }
    }
    if (start < 208 && start + count > 64)
    {
        const int lo = (int)start < 64 ? 64 : (int)start;
        const int hi = (int)(start + count) > 208 ? 208 : (int)(start + count);
        trkW.boneSerial++;
        memcpy(trkW.boneShadow + (lo - 64) * 4, data + (lo - (int)start) * 4, (size_t)(hi - lo) * 16);
        if (hi - 1 - 64 > trkW.boneMaxReg) trkW.boneMaxReg = hi - 1 - 64;
        if (start <= 64 && start + count >= 68 && count <= 16) { memcpy(trkW.wheel, data + (64 - (int)start) * 4, 64); trkW.haveWheel = true; }
        else if (lo < 68) trkW.haveWheel = false;
    }
}
static void TrkObsStream(const DWORD* c)
{
    if (c[1] == 0) { trkW.vb = c[2]; trkW.vbOff = c[3]; trkW.vbStr = c[4]; }
    else if (c[1] <= VEL_XSTREAMS) { trkW.xvb[c[1] - 1] = c[2]; trkW.xoff[c[1] - 1] = c[3]; trkW.xstr[c[1] - 1] = c[4]; }   // as hkSetStreamSource
}
static void TrkObsIndices(const DWORD* c)  { trkW.ib = c[1]; }
static void TrkObsDS(const DWORD* c)       { trkW.ds = (IDirect3DSurface9*)(UINT_PTR)c[1]; }
static void TrkObsDecl(const DWORD* c)
{
    IDirect3DVertexDeclaration9* decl = (IDirect3DVertexDeclaration9*)(UINT_PTR)c[1];
    if (dead) return;
    trkW.skinned = false; trkW.posT = false; trkW.xmask = 0; trkW.decl = decl;
    if (!decl) return;
    DEVSTAT(trkWDeclAsk++;)
    const unsigned dslot = ((unsigned)(UINT_PTR)decl >> 4) & 63u;
    {
        const int k = trkWDeclIdx[dslot] - 1;
        if (k >= 0 && trkWDeclPtr[k] == decl)
        {
            trkW.skinned = (trkWDeclFlags[k] & 1) != 0; trkW.posT = (trkWDeclFlags[k] & 2) != 0;
            trkW.xmask = TrkXMaskOf(trkWDeclFlags[k]);
            return;
        }
    }
    DEVSTAT(trkWDeclScan++;)
    for (int i = 0; i < trkWDeclN; i++)
        if (trkWDeclPtr[i] == decl)
        {
            trkWDeclIdx[dslot] = i + 1;
            trkW.skinned = (trkWDeclFlags[i] & 1) != 0; trkW.posT = (trkWDeclFlags[i] & 2) != 0;
            trkW.xmask = TrkXMaskOf(trkWDeclFlags[i]);
            return;
        }
    bool hasIdx = false, hasWt = false, posT = false;
    unsigned char xmask = 0;
        // 65, not 64: D3D9 allows MAXD3DDECLLENGTH (64) real elements plus the D3DDECL_END sentinel, and GetDeclaration writes all
    // of them with no bound of its own.
    D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1]; UINT n = 0;
    if (SUCCEEDED(decl->GetDeclaration(el, &n)))
        for (UINT e = 0; e + 1 < n; e++)
        {
            if (el[e].Usage == D3DDECLUSAGE_BLENDINDICES) hasIdx = true;
            if (el[e].Usage == D3DDECLUSAGE_BLENDWEIGHT)  hasWt  = true;
            if (el[e].Usage == D3DDECLUSAGE_POSITIONT)    posT   = true;
            if (el[e].Stream != 0)                      xmask |= VelXMaskBit(el[e].Stream);
        }
    trkW.skinned = hasIdx && hasWt; trkW.posT = posT; trkW.xmask = xmask;
    if (trkWDeclN < VEL_DECL_CACHE)
    {
        decl->AddRef();
        trkWDeclPtr[trkWDeclN] = decl;
        trkWDeclFlags[trkWDeclN] = (unsigned char)((trkW.skinned ? 1 : 0) | (posT ? 2 : 0) | TrkFlagsOfXMask(xmask));
        trkWDeclIdx[dslot] = trkWDeclN + 1;
        trkWDeclN++;
    }
    DEVSTAT(else trkWDeclPastCap++;)
}
static void TrkObsVS(const DWORD* c)
{
    IDirect3DVertexShader9* vs = (IDirect3DVertexShader9*)(UINT_PTR)c[1];
    if (dead) return;
    trkW.premul = false; trkW.vs = vs; trkW.texld0 = 0;
    if (!vs) return;
    DEVSTAT(trkWVSAsk++;)
    const unsigned vslot = ((unsigned)(UINT_PTR)vs >> 4) & 63u;
    {
        const int k = trkWVSIdx[vslot] - 1;
        if (k >= 0 && trkWVSPtr[k] == vs) { trkW.premul = trkWVSPremul[k]; trkW.texld0 = trkWVSTexld0[k]; return; }
    }
    DEVSTAT(trkWVSScan++;)
    for (int i = 0; i < trkWVSN; i++)
        if (trkWVSPtr[i] == vs) { trkWVSIdx[vslot] = i + 1; trkW.premul = trkWVSPremul[i]; trkW.texld0 = trkWVSTexld0[i]; return; }
    int instr = 0; unsigned m64 = 0, m8 = 0; bool rel = false, pm = false, pw = false, premul = false;
    __try { premul = FingerprintVS(vs, &instr, &m64, &m8, &rel, &pm, &pw); }
    __except (EXCEPTION_EXECUTE_HANDLER) { premul = false; }
    trkW.premul = premul;
    {   // the texldl count on vertex sampler 0, as the render side's cache has it (DLSS-IV.cpp, VsCountTexldl)
        BYTE t0 = 0; bool straight = false;
        __try { (void)VsCountTexldl(vs, &t0, &straight); }
        __except (EXCEPTION_EXECUTE_HANDLER) { t0 = 0; }
        trkW.texld0 = t0;
    }
    if (trkWVSN < VEL_VS_CACHE) { vs->AddRef(); trkWVSPtr[trkWVSN] = vs; trkWVSPremul[trkWVSN] = premul; trkWVSTexld0[trkWVSN] = trkW.texld0; trkWVSIdx[vslot] = trkWVSN + 1; trkWVSN++; }
    DEVSTAT(else trkWVSPastCap++;)
}
#if DLSSIV_DEVLOG
// The velocity caches against their caps: each thread's shader and declaration tables, how often the device thread's 64-slot
// index sent a bind to the scan, and fingerprints repeated because a table was full. From DtReport every 600 frames at log=2;
// held while the counts stand still.
static void TrkCacheReport(double n)
{
    static LONG base[4] = {};
    const LONG va = trkWVSAsk - base[0], vs = trkWVSScan - base[1], da = trkWDeclAsk - base[2], ds = trkWDeclScan - base[3];
    base[0] = trkWVSAsk; base[1] = trkWVSScan; base[2] = trkWDeclAsk; base[3] = trkWDeclScan;
    static LogHold hold;
    if (LOGHELD(hold, false, velVSCount, trkWVSN, velDeclCount, trkWDeclN, velVSPastCap, trkWVSPastCap, velDeclPastCap, trkWDeclPastCap))
        return;
    LogHealth("    velocity caches (N5): vertex shaders %d of %d on the render thread and %d on the device thread, declarations %d of %d"
              " and %d; the device thread's index sent %.1f of %.1f shader binds and %.1f of %.1f declaration binds a frame on to the"
              " scan; past the caps this session %ld and %ld shaders, %ld and %ld declarations fingerprinted again%s",
              velVSCount, VEL_VS_CACHE, trkWVSN, velDeclCount, VEL_DECL_CACHE, trkWDeclN, vs / n, va / n, ds / n, da / n,
              (long)velVSPastCap, (long)trkWVSPastCap, (long)velDeclPastCap, (long)trkWDeclPastCap, LOGMARK);
}
#endif
// DrawIndexedPrimitive: the tracker's gate as hkDrawIndexed applies it (!dead, the verdict
// latched, a world captured, the scene level), then the same two bodies the render thread
// calls, back to back. The worker has replayed the draw, so VelRecordObject's two Gets read
// the state that draw ran with, exactly as on the render thread, where it runs after the
// game's draw too.
static void TrkObsDraw(const DWORD* c)
{
    if (!trkWorkerLive || dead || !trkW.latched || !trkW.haveWorld || !trkW.sceneNow) return;
    TrkOnDrawPre<TrkWorker>((D3DPRIMITIVETYPE)c[1], (INT)c[2], c[3], c[4], c[5], c[6]);
    if (trkWPend)
    {
        trkWPend = false;
        VelRecordObject<TrkWorker>(dtDev, &trkWPendWVP.m[0][0], trkWPendType, trkWPendBase, trkWPendMin, trkWPendNum,
                                   trkWPendStart, trkWPendPrim, trkWPendBoneRow, trkWPendPremul ? &trkWPendWVP8.m[0][0] : nullptr);
        trkWPendPremul = false;
    }
}
// EndScene: what hkEndScene does for the tracker at the frame's end, at the recorded
// EndScene's place, when this thread owns it. The frame note has already been applied (it
// is recorded before EndScene and nothing is drawn between). BoneTexUpload's LockRect from
// the worker goes straight to DXVK (dt_res.h: DtForeignThread excludes the worker); the drop's
// Releases do too.
static void TrkObsEndScene(const DWORD*)
{
        // `dead` too, as hkEndScene's frame-end block and every other observer here: after the mod has stopped, the worker must not
    // upload the pose texture or drop the velocity batch.
    if (!trkWorkerLive || dead) return;
    M4dEndFrame();
    BoneTexUpload();
    VelBatchDrop();
}

// ---- the owner changes (render thread, at drained points) -----------------------------------
// The worker's copies become the render thread's, byte for byte. Nothing is recorded between
// this and the worker's next command, so the observers continue from exactly this state.
static void TrkSeedWorker()
{
    memcpy(trkW.world,   m4bCurWorld,   64); trkW.haveWorld   = m4bHaveWorld;
    memcpy(trkW.wvp,     m4bCurWVP,     64); trkW.haveWVP     = m4bHaveWVP;
    memcpy(trkW.viewInv, m4bCurViewInv, 64); trkW.haveViewInv = m4bHaveViewInv;
    memcpy(trkW.wheel,   m4bCurWheel,   64); trkW.haveWheel   = m4bHaveWheel;
    trkW.worldSerial = m4bWorldSerial;
    memcpy(trkW.boneShadow, boneShadow, sizeof(trkW.boneShadow));
    trkW.boneMaxReg = boneMaxReg; trkW.boneSerial = boneShadowSerial;
    trkW.vb = (UINT_PTR)velCurVB; trkW.vbOff = velCurVBOff; trkW.vbStr = velCurVBStr; trkW.ib = (UINT_PTR)velCurIB;
    trkW.decl = velCurDecl; trkW.vs = velCurVS; trkW.ds = curDS;
    trkW.skinned = velCurSkinned; trkW.posT = velCurPosT; trkW.premul = velCurVSPremul; trkW.texld0 = velCurVSTexld0;
    trkW.xmask = velCurXMask;
    for (int s = 0; s < VEL_XSTREAMS; s++) { trkW.xvb[s] = (UINT_PTR)velCurXVB[s]; trkW.xoff[s] = velCurXOff[s]; trkW.xstr[s] = velCurXStr[s]; }
    trkW.latched = toldVerdict != 0; trkW.sceneNow = trkRSceneSent; trkW.window = m4dWindow;
    trkW.camPrevVP = camPrevVPd; trkW.camPrevValid = camPrev.valid; trkW.frames = frames; trkW.armCount = dtArmCount;
    trkWPend = false; trkWPendPremul = false;
}
// At the arm (dt_install.h, before dtArmed goes up, ring empty, worker idle): seed, hand over.
static void TrkOnArm()
{
    TrkSeedWorker();
    trkWorkerLive = true;
}
// At the disarm tail (flag down, ring drained): the render thread owns everything again. Its
// own copies were never stale, its hooks captured all along.
static void TrkOnDisarm()
{
    trkWorkerLive = false;
    trkWPend = false;
}

// ---- the notes (worker) ---------------------------------------------------------------
static void TrkNoteWindowW(DWORD, const DWORD* p, DWORD bytes) { if (bytes >= 4) trkW.window = *(const float*)p; }
static void TrkNoteSceneW(DWORD, const DWORD* p, DWORD bytes)  { if (bytes >= 1) trkW.sceneNow = *(const unsigned char*)p != 0; }
static void TrkNoteFrameW(DWORD, const DWORD* p, DWORD bytes)
{
    if (bytes < sizeof(TrkFrameNote)) return;
    const TrkFrameNote* n = (const TrkFrameNote*)p;
        // A note from an arm this side never saw: the seed at every arm makes this unreachable; if it is ever taken, start from
    // empty copies rather than stale ones, and say so once.
    if (n->armCount != trkW.armCount)
    {
        const LONG keepArm = n->armCount, hadArm = trkW.armCount;
        memset(&trkW, 0, sizeof(trkW));
        trkW.boneMaxReg = -1;
        trkW.armCount = keepArm;
        static bool said = false;
        if (!said) { said = true; LogRecord("  threaded: the tracker's worker copies met a frame note from arm %ld while holding arm %ld; started clean", keepArm, hadArm); }
    }
    trkW.camPrevVP    = n->vp;
    trkW.window       = n->window;
    trkW.sceneNow     = n->sceneNow != 0;
    trkW.latched      = n->latched != 0;
    trkW.frames       = n->frames;
    trkW.camPrevValid = n->camPrevValid != 0;
}

// ---- the render thread's side ---------------------------------------------------------
// Installed BEFORE the layer, so the worker's first command is observed. Plain stores
// into tables the worker reads; the layer arms 300 presents later.
static void TrkArm()
{
    trkW.boneMaxReg = -1;
    dtObs[94]  = TrkObsVsConstF;
    dtObs[100] = TrkObsStream;
    dtObs[104] = TrkObsIndices;
    dtObs[87]  = TrkObsDecl;
    dtObs[92]  = TrkObsVS;
    dtObs[39]  = TrkObsDS;
    dtObs[82]  = TrkObsDraw;
    dtObs[42]  = TrkObsEndScene;
    dtArmObs    = TrkOnArm;
    dtDisarmObs = TrkOnDisarm;
    dtNoteObs[DT_NOTE_TRK_WINDOW] = TrkNoteWindowW;
    dtNoteObs[DT_NOTE_TRK_SCENE]  = TrkNoteSceneW;
    dtNoteObs[DT_NOTE_TRK_FRAME]  = TrkNoteFrameW;
        // The injection on the worker. Registered here beside the tracker's because this is the one place that installs note
    // observers; the handler is the transport's.
    dtNoteObs[DT_NOTE_INJECT]     = IvtInjectNoteW;
        // It is the one note whose body drives the device, so the shadow must not fetch an entry it touches while one is out
    // (dt_core.h, dtNoteDrives). From the body's whole call tree (IvtSaveState's set, IvtSetupPass, VelDrain) it touches: render
    // targets, depth, shaders, declaration, streams 0..3, indices, viewport, render states, c0..c8, the c64 bone palette, c247,
    // and texture and sampler slots 0, 1, 2 and vertex samplers 0 and 1 only (VelDmgBind puts a dented vehicle's damage map on
    // vertex sampler 1). Every other sampler slot is declared free, which lets the HDR capture's sixteen-slot sweep run beside
    // the body. A new bind in the body on another slot must come off this mask, or a fetch beside it can hand the game the
    // mod's texture.
    dtNoteDrives[DT_NOTE_INJECT]  = true;
        // And the render states: every one this mod sets anywhere, body or not, a superset that costs nothing (all literals in the
    // source; tools/check-body-touch.py fails the build check if a new one appears or one is set through a variable).
    {
        static const DWORD kBodyRs[] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_ZFUNC,
            D3DRS_ALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
            D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP };   // the rain layer is added to the picture (transport/rain_pass.h)
        memset(dtBodyRsTouched, 0, sizeof(dtBodyRsTouched));
        for (DWORD s : kBodyRs) dtBodyRsTouched[s >> 5] |= 1u << (s & 31);
    }
    dtBodySampFree = 0x1FFFFFu & ~(1u << 0 | 1u << 1 | 1u << 2 | 1u << DtSampIdx(D3DVERTEXTEXTURESAMPLER0) | 1u << DtSampIdx(D3DVERTEXTEXTURESAMPLER1));
}
// A refused note is a level the worker holds stale for a frame (the frame note resends both
// levels, so it heals): counted, and said once.
static void TrkNoteRefused()
{
    if (!trkRNotesRefusedSaid)
    {
        trkRNotesRefusedSaid = true;
        LogRecord("  threaded: a tracker note was refused by the ring (it heals at the next frame note). Said once.");
    }
}
// From hkDrawIndexed's gate, once a frame, after M4dUpdateWindow: the window as this frame uses it.
static void TrkSendWindow()
{
    if (!dtArmed) return;
    if (!DtNote(DT_NOTE_TRK_WINDOW, &m4dWindow, sizeof(float))) TrkNoteRefused();
}
// From hkDrawIndexed, once sceneNow is final and BEFORE the draw is recorded, as a level.
static void TrkSendScene(bool sceneNow)
{
    if (!dtArmed) { trkRSceneSent = sceneNow; return; }
    if (sceneNow == trkRSceneSent) return;
    trkRSceneSent = sceneNow;
    const unsigned char b = sceneNow ? 1 : 0;
    if (!DtNote(DT_NOTE_TRK_SCENE, &b, 1)) TrkNoteRefused();
}
// From hkEndScene, after camPrevVPd is formed and before EndScene is recorded.
static void TrkSendFrame()
{
    if (!dtArmed) return;
    TrkFrameNote n;
    n.vp = camPrevVPd; n.window = m4dWindow; n.frames = frames; n.armCount = dtArmCount;
    n.sceneNow = trkRSceneSent ? 1 : 0; n.latched = toldVerdict ? 1 : 0; n.camPrevValid = camPrev.valid ? 1 : 0; n.pad = 0;
    if (!DtNote(DT_NOTE_TRK_FRAME, &n, sizeof(n))) TrkNoteRefused();
}
