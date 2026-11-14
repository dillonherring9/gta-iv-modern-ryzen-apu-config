// Frame generation's binding that arms. fg_product.h's FgP is the product's base binding; FgPA, here, derives from it, so the
// wire, the pipe's writer rule and the gate before DXVK's Present are FgP's own lines. It adds what the merged path asks of a
// binding that arms (fg_machine.h, "A BINDING THAT ARMS ON THE WIRE"): its state, its command ring and copies, "is this image
// there", the back buffer's query, mark and question, the copy into the back buffer, FgPace's wait and its sleeper count, the
// frame end, the three wakes and the placer's poll.
// The product is the machine over it in every build that carries frame generation (fg_product.h, FgPM). tools/test-fg-arms
// compiles both configurations and holds what each carries.
// The command ring and the copies are fg_vklines.h's, unchanged. What it asks of its includer:
//   - FGV_ASSERT_TRANSPORT_SCOPES() beside the include: below.
//   - tools/check-dt.py's list has fg_vklines.h, this file and fg_arm_life.h.
//   - FgvRecSubmit returns the VkResult: a refused submit ends frame generation for the session (FgPA::RecSubmit kills the
//     machine; the conditions then say "off for this session").
//   - A write that was not submitted presents nothing: the machine's (ArmedFirePresent returns before its Present), resting
//     on ImgOk and RecSubmit answering truly here.
//   - The one-image flush stays: the machine's too (its fire flushes before the restore when the image did not rotate).
//   - The ring is 32 deep (FGV_RING).
// The mod's own two images, HOLD and SAVE, live with the back buffer: made at its size and format, remade when a build finds
// another back buffer, never touched by a build that only releases the helper's set. Their life is fg_arm_life.h's; this
// file holds them and answers for them (ImgOk).
// The rules are fg_product.h's: every seam that talks to DXVK opens DT_DIRECT; nothing here stops the transport; no
// function-local static and no const table.
#pragma once
#include "fg_vklines.h"
FGV_ASSERT_TRANSPORT_SCOPES();
#include "fg_cond.h"
#include "fg_plan.h"
#include "fg_threads.h"
// The set's places and the machine's numbers (feed_ipc.h, FEED_FG_GEN2; fg_machine.h, FgImg). The machine numbers GEN2 3,
// SAVE 4 and HOLD 5; the set's fourth place is GEN2. Img and ImgOk below name SAVE and HOLD before they index the set, so a
// copy naming SAVE reaches the mod's own image.
static_assert(FG_IMG_PRESENT == FEED_FG_PRESENT && FG_IMG_GEN == FEED_FG_GEN && FG_IMG_HUDLESS == FEED_FG_HUDLESS && FG_IMG_SAVE != FG_IMG_HOLD
              && FG_IMG_SAVE > FG_IMG_HUDLESS && FG_IMG_HOLD > FG_IMG_HUDLESS
              && (FG_IMG_SAVE == FEED_FG_GEN2 || (FG_IMG_SAVE >= FEED_FG_IMAGES && FG_IMG_HOLD >= FEED_FG_IMAGES)),
              "the set's three images are the machine's first three, SAVE and HOLD are two more, named before the set is indexed (feed_ipc.h, fg_machine.h)");
static_assert(FG_HOLD_SLOTS == 1, "one hold slot, HOLD (fg_machine.h)");

// THE HUD-LESS PICTURE at hdr=1, the game's side.
#include "fg_hud.h"
#include "fg_web.h"     // the game's web page: a menu stand-by

// What the armed binding keeps, in the product's one allocation behind FgpSide. Zeroed by VirtualAlloc.
struct FgpArm
{
    FgArmedState a;
        // ---- the worker's ring, and the render thread's buffer for a first layout
    FgvRing         ring;
    VkCommandPool   pool, poolOnce;
    VkCommandBuffer cbOnce;
        // ---- HOLD and SAVE, and the back buffer they were made for (hold[0] HOLD, hold[1] HOLD1, hold[2] HOLD1 + 1)
    FgvImage        hold[FG_HOLD_SLOTS], save;
    UINT            iw, ih; VkFormat ifmt;
    VkPhysicalDeviceMemoryProperties mp; LONG mpRead;
    LONG            imgMade, imgLeft;   // made; left to the device unretired
    LONG            layoutFlight;       // a first layout had not retired in 5 s
        // ---- the threads' own (fg_threads.h)
    HANDLE          placerTimer, paceEv, paceTimer, thrEnd;
        // ---- the conditions (fg_cond.h)
    FgCondK         condK;
    FgCondIn        condIn;             // as a build or Reset last read it
    SRWLOCK         condLock;           // render thread and latency waiter: one step at a time
    LONG            condRead;           // until set, the waiter's step asks nothing
    volatile LONG   refusal;            // standing row (FGC_*, 0 none), loaded at every present
    volatile LONG   menuSeen;
        // ---- the plan (fg_plan.h)
    FgPlanK         planK;
    LONG            planSeen[FG_K_BINS];
    LONG            planWin[FG_PLAN_WINDOWS][FG_K_BINS];
    LONG            planAt;
    FgPlanMem       planMem;
    LONG            lateFireUs;         // reserved
        // ---- the driver's (sole writer of the want): windows the want has been PASS
    LONG            passWindows;
        // ---- the latency waiter's route for a frame whose end the machine owns
    FgRoute         route;
        // ---- the helper's answer on a card that is the limit
    LONG            homeN;              // worker's: last frame whose home was submitted behind its helper wait (0 at a build)
    FgxCard         card;
    FgxOrd          ord;                // made at the install, never destroyed
        // ---- the worker's GPU stamps, last: the machine places a ride pair's generated frame by them
    FgvTs           ts;
    FgvTsPace       tsPace;
};
static void FgpTsTake(int kind, LONG frame, UINT64 head, UINT64 tail);
static __forceinline FgpArm* FgpArmOf() { return (FgpArm*)((BYTE*)fgp.side + ((sizeof(FgpSide) + 63) & ~(SIZE_T)63)); }

// fg_vklines.h's binding: the transport's own Vulkan table and DXVK's queue.
struct FgpV
{
    static FeedVk*  Vk() { return &ivtVk; }
    static VkResult CbReset(VkCommandBuffer cb) { return ivtResetCommandBuffer(cb, 0); }
    static VkResult CbBegin(VkCommandBuffer cb, const VkCommandBufferBeginInfo* bi) { return ivtBeginCommandBuffer(cb, bi); }
    static VkResult CbEnd(VkCommandBuffer cb) { return ivtEndCommandBuffer(cb); }
    static VkResult Submit(const VkSubmitInfo* si) { DT_DIRECT(); return ivtQueueSubmit(ivtQueue, 1, si, VK_NULL_HANDLE); }   // the caller holds the submission lock
    static LONGLONG Now() { return FgP::Now(); }
    static LONGLONG Qpf() { return FgP::Qpf(); }
};

// ---- the binding
struct FgPA : FgP
{
    static constexpr bool kRig = false, kEngage = true, kWire = true;
        // Zeroed at the install; its keys are written there before the first pair (FgpArmKeys).
    static FgArmedState& A() { return FgpArmOf()->a; }
        // FgP's facts, no condition row standing, and no menu up (the game's own flag, so the stand-by starts at this present;
    // where the flag was not found there is no stand-by).
    static __forceinline bool MayEngage()
    {
        if (!FgP::MayEngage()) return false;
        FgpArm& m = *FgpArmOf();
        if (m.refusal) return false;
        if (GameMenuUp()) { if (!m.menuSeen) m.menuSeen = 1; return false; }
                // A game web page is up (fg_web.h): stand by as for a menu.
        if (FgWebUp()) { if (!m.menuSeen) m.menuSeen = 1; return false; }
        return true;
    }
        // The machine's seams for a card that is the limit. Each answers no unless FgpArmKeys sets its key (over, card, ord).
    // HomeWaited: written only by the transport's injection body behind the submit of a home that waits on that frame's out-fence
    // (IvtInjectBody); every other path leaves the previous number, so the answer is no. Asked on the worker, which writes it.
    static bool HomeWaited(LONG n) { return n > 0 && FgpArmOf()->homeN == n; }
        // CardClass: on the watch's duplicate of the helper handle (never closed, so another thread's stop cannot close it under this
    // call), else the transport's. Any thread.
    // OrdWait: submitted raw inside the hand's span, under the submission lock it holds. OrdSignal: vkSignalSemaphore on this side's
    // fence from any thread; takes no DXVK lock. Neither inside DllMain's detach.
    static bool PlaceStamps() { return FgpArmOf()->ts.pool != VK_NULL_HANDLE; }
    static void CardClass(int up) { if (fgp.quiet) return; FgxCardClass(ivtWatchHost ? ivtWatchHost : ivtHost, up != 0, &FgpArmOf()->card); }
    static bool OrdWait(LONG n) { return FgxOrdWait(&FgpArmOf()->ord, (uint64_t)(ULONG)n, &FgpV::Submit); }
    static void OrdSignal(LONG n) { if (fgp.quiet) return; FgxOrdSignal<FgxB>(&FgpArmOf()->ord, (uint64_t)(ULONG)n); }

        // ---- worker spans over the ring; the machine owns their order
    static bool CbTake(int n) { return FgvCbTake<FgpV>(FgpArmOf()->ring, n); }
        // The slot gives up its last two stamps, then its new buffer gets its head stamp (fg_vklines.h, "the spans on the card").
    static void RecBegin()
    {
        FgpArm& m = *FgpArmOf();
        FgvRecBegin<FgpV>(m.ring);
        int kind = 0; LONG frame = 0; UINT64 head = 0, tail = 0;
        if (FgvTsRead(m.ts, &ivtVk, m.ring.curSlot, &kind, &frame, &head, &tail)) FgpTsTake(kind, frame, head, tail);
        FgvTsHead(m.ts, m.ring);
    }
        // Which Vulkan image a machine image number names, and whether it is the helper's (acquired and released round a copy).
    // SAVE and HOLD, the mod's own, are tested first (the assert at this file's head).
    static VkImage Img(int img, bool* external)
    {
        FgpArm& m = *FgpArmOf();
        *external = false;
        if (img == FG_IMG_SAVE) return m.save.img;
        if (img == FG_IMG_HOLD) return m.hold[0].img;
        *external = true;
        return (img >= 0 && img < FEED_FG_IMAGES) ? fgx.c->set.img[img] : VK_NULL_HANDLE;
    }
        // Whether a copy may name this image now. HOLD and SAVE count only if made for this build's back buffer; else the machine presents
    // every pair inline.
    static bool ImgOk(int img)
    {
        const FgpArm& m = *FgpArmOf(); const FgpVk& v = fgp.side->v;
        if (img == FG_IMG_SAVE || img == FG_IMG_HOLD)
            return (img == FG_IMG_SAVE ? m.save.img : m.hold[0].img) != VK_NULL_HANDLE && m.iw == v.w && m.ih == v.h && m.ifmt == v.fmt;
        return img >= 0 && img < FEED_FG_IMAGES && fgx.c->set.img[img] != VK_NULL_HANDLE;
    }
        // Never cached. A back buffer not of this set's size or format ends frame generation rather than copy out of bounds.
    static ULONGLONG BBImage()
    {
        DT_DIRECT();
        FgpVk& v = fgp.side->v;
        VkImage i = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED; UINT w = 0, h = 0; VkFormat f = VK_FORMAT_UNDEFINED;
        if (!FgvBbQuery(backbufferSurf, &i, &l, &w, &h, &f)) { FgpDead("the back buffer's Vulkan image could not be read on the worker"); return 0; }
        if (w != v.w || h != v.h || f != v.fmt)
        {
            char why[176];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "the back buffer is %ux%u VkFormat %d and this build's set was made for %ux%u VkFormat %d", w, h, (int)f, v.w, v.h, (int)v.fmt);
            FgpDead(why); return 0;
        }
        v.bbLayout = l;
        return (ULONGLONG)i;
    }
    static void RecCopyToImg(ULONGLONG bb, int img)
    {
        DT_DIRECT();
        const FgpVk& v = fgp.side->v;
        bool ext = false; const VkImage i = Img(img, &ext);
        FgvRecCopyOut(&ivtVk, FgpArmOf()->ring.cur, (VkImage)bb, v.bbLayout, i, ext, ivtQueueFamily, v.w, v.h);
        FgpArmOf()->ts.copies |= img == FG_IMG_PRESENT ? FGV_TC_TO_PRESENT : img == FG_IMG_HUDLESS ? FGV_TC_TO_HUDLESS : FGV_TC_TO_OWN;
    }
    static void RecCopyFromImg(int img, ULONGLONG bb)
    {
        DT_DIRECT();
        const FgpVk& v = fgp.side->v;
        bool ext = false; const VkImage i = Img(img, &ext);
        FgvRecCopyIn(&ivtVk, FgpArmOf()->ring.cur, i, ext, ivtQueueFamily, (VkImage)bb, v.bbLayout, v.w, v.h);
        static_assert(FG_IMG_GEN2 == FEED_FG_GEN2 && FG_IMG_SAVE != FG_IMG_GEN2, "GEN2 is the set's and not SAVE (fg_machine.h, FgImg)");
        FgpArmOf()->ts.copies |= (img == FG_IMG_GEN || img == FG_IMG_GEN2) ? FGV_TC_FROM_GEN : img == FG_IMG_SAVE ? FGV_TC_FROM_SAVE : FGV_TC_FROM_STORE;
    }
    static void RecSignal(int tl, LONG val)
    {
        FgvRing& g = FgpArmOf()->ring;
        g.pendSem = tl == FG_TL_IN ? fgx.c->set.semIn : VK_NULL_HANDLE;
        g.pendVal = (UINT64)(ULONG)val;
    }
        // Under the machine's submission lock. A refused submit ends frame generation for the session, never the transport.
    static bool RecSubmit()
    {
        DT_DIRECT();
        FgvTsTail(FgpArmOf()->ts, FgpArmOf()->ring, fgp.s->w.openFEnd);
        const int tsSlot = FgpArmOf()->ring.curSlot;
        const VkResult r = FgvRecSubmit<FgpV>(FgpArmOf()->ring);
        if (r != VK_SUCCESS) FgvTsRefused(FgpArmOf()->ts, tsSlot);
        if (r == VK_SUCCESS) return true;
        char why[96]; _snprintf_s(why, sizeof(why), _TRUNCATE, "a submit of its copies was refused (VkResult %d)", (int)r);
        FgpDead(why);
        return false;
    }
        // Every submit of this ring the queue took has retired.
    static bool RetireWait(DWORD ms) { return FgvRetireWait<FgpV>(FgpArmOf()->ring, ms); }
        // Whether the back buffer was written, answered by the device thread (devthread/dt_bbs.h); what it cannot see answers
    // "written". A present with a rectangle may go through GDI with a DC on the back buffer, which nothing sees.
    static void BackMark() { DtBbsMark(); }
    static bool BackWritten()
    {
        const FgWorkerLine& w = fgp.s->w;
        const bool written = DtBbsWritten(backbufferSurf, w.fireKind, w.hasSrc || w.hasDst);
        return written;
    }
        // THE HUD-LESS PICTURE (fg_machine.h, "THE HUD-LESS PICTURE"), on the render thread inside the machine's Present.
    // A pass is promised only where HOLD and HUDLESS both exist; HudUndo redraws the real encode when the note was refused.
    static IDirect3DDevice9* HudDev() { return ivtDev ? ivtDev : hdrArmDev; }
    static int  HudWant() { return FgHudWant(HudDev(), ImgOk(FG_IMG_HOLD) && ImgOk(FG_IMG_HUDLESS)); }
    static void HudDraw() { FgHudDraw(HudDev()); }
    static void HudUndo() { FgHudUndo(HudDev()); }
        // On the worker inside the hand's span. The second target's Vulkan image and layout are asked after this span's flush, never
    // cached: DXVK's defragmenter can move the texture to a new image (FgHudSrcTake).
    static bool HudSrc() { const FgpVk& v = fgp.side->v; return FgHudSrcOk(v.w, v.h, v.fmt); }
    static void RecCopyHudToImg(int img)
    {
        DT_DIRECT();
        const FgpVk& v = fgp.side->v;
        bool ext = false; const VkImage i = Img(img, &ext);
        FgvRecCopyOut(&ivtVk, FgpArmOf()->ring.cur, fgHud.srcImg, fgHud.srcLayout, i, ext, ivtQueueFamily, v.w, v.h);
        FgpArmOf()->ts.copies |= FGV_TC_TO_HUDLESS;
    }
        // ---- timelines: the helper's three, and the frame's end
    static VkSemaphore TlSem(int tl) { return tl == FG_TL_END ? latSemEnd : FgP::TlSem(tl); }
        // Forced: the compiler does not inline it into the per-pair functions by itself.
    static __forceinline ULONGLONG TlValue(int tl) { const VkSemaphore s = TlSem(tl); return s ? FeedVkTimelineValue(&ivtVk, s) : 0; }
    static bool TlWait(int tl, ULONGLONG val, DWORD ms)
    {
        const VkSemaphore s = TlSem(tl);
        const VkResult r = s ? FeedVkWaitTimelineR(&ivtVk, s, val, ms) : VK_NOT_READY;
        if (r == VK_SUCCESS) return true;
        if (r != VK_TIMEOUT) Sleep(ms);
        return false;
    }
        // ---- seams shared with the rig
    static LONG PaceSleptN() { return latPaceSleptN; }
        // Render thread. The placer's wake is tested first so one already set wins; 1 when it woke. Bounded, so a timer that never fires
    // cannot hold the game.
    static int PaceWait(LONGLONG dueQpc)
    {
        FgpArm& m = *FgpArmOf();
        if (!m.paceEv || !m.paceTimer) return 0;
        LONGLONG left = dueQpc - Now();
        if (left < 0) left = 0;
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((double)left * 1e7 / (double)Qpf());
        if (due.QuadPart == 0) due.QuadPart = -1;
        if (!SetWaitableTimer(m.paceTimer, &due, 0, nullptr, nullptr, FALSE)) return 0;
        const HANDLE h[2] = { m.paceEv, m.paceTimer };
        const DWORD r = WaitForMultipleObjects(2, h, FALSE, 100);
        if (r == WAIT_OBJECT_0) { CancelWaitableTimer(m.paceTimer); return 1; }
        return 0;
    }
        // Polled without a wait for FgPace's last spin: 1 when the placer woke it (the auto-reset event is taken).
    static int PacePoll() { const HANDLE e = FgpArmOf()->paceEv; return (e && WaitForSingleObject(e, 0) == WAIT_OBJECT_0) ? 1 : 0; }
    static void LatEnd(LONG f) { LatEndSignal(f); }
    static void WakePace()   { const HANDLE e = FgpArmOf()->paceEv; if (e) SetEvent(e); }
    static bool PlacerPoll() { return fgp.placerEv && WaitForSingleObject(fgp.placerEv, 0) == WAIT_OBJECT_0; }
};
// the second target is given back only behind this ring's retire
static bool FgHudRetireWait(DWORD ms) { return !fgp.side || FgPA::RetireWait(ms); }

// ---- what the armed binding's threads wait on
struct FgpThr
{
    static bool Quit() { return fgp.quit != 0; }
    static void PlacerWait()
    {
        const HANDLE t = FgpArmOf()->placerTimer;
        const HANDLE hs[3] = { fgp.placerEv, fgp.quitEv, t };
        WaitForMultipleObjects(t ? 3 : 2, hs, FALSE, INFINITE);
    }
    static void PlacerTimer(LONGLONG at)
    {
        const HANDLE t = FgpArmOf()->placerTimer;
        if (!t) return;
        const LONGLONG d = at - FgP::Now();
        LARGE_INTEGER due; due.QuadPart = d > 0 ? -(d * 10000000 / FgP::Qpf()) : -1;
        if (due.QuadPart == 0) due.QuadPart = -1;
        SetWaitableTimer(t, &due, 0, nullptr, nullptr, FALSE);
    }
    static void PlacerRan() {}
    static void RunWait()  { const HANDLE hs[2] = { fgp.runEv, fgp.quitEv }; WaitForMultipleObjects(2, hs, FALSE, INFINITE); }
    static void GoneWait() { WaitForSingleObject(fgp.quitEv, 250); }
    static LONG StampFrom(int tl) { return (LONG)FgPA::TlValue(tl); }
    static void StamperUp(int tl) { (void)tl; }
    static void WakePlacer() { SetEvent(fgp.placerEv); }
};
