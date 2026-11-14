// fg_xport.h: frame generation's transport, the game's side: the 'g' exchange and its ack, the imports of the set's
// images and (once a session) the three fences, the release, and the per-pair half below. Over a binding so the same
// lines run in the mod and in tools/test-fg-pipe (a 32-bit stand-in for the game against the built helper).
// The binding B, all static:
// bool    PipeWrite(const void* p, DWORD n)                         one write of a whole message
// bool    PipeReadWithin(void* p, DWORD n, DWORD ms, bool* late)    a read with a deadline; *late when the deadline passed
// FeedVk* Vk()                                                      the device's loaded functions (transport/feed_vk.h)
// VkExternalMemoryHandleTypeFlagBits HandleType()                   the handle type the session's imports settled on
// bool    Narrow()                                                  narrow the memory type by the handle's own properties
// void    Line(const char* fmt, ...)                                one line in the log
// The fences are imported once a session and never destroyed, so their readers take no lock; a later ack with other
// handles is refused. Images are closed with their handles (Vulkan does not own an imported handle). A failed import
// refuses frame generation for this set, never the transport; late and a broken pipe stop the transport.
// Included at the end of DLSS-IV.cpp, after feed_ipc.h, feed_fg.h and feed_vk.h.
#pragma once
#ifndef FEED_FG_IMAGES
#error "fg_xport.h needs feed_ipc.h and feed_fg.h before it"
#endif

struct FgxSet
{
        // Once a session.
    VkSemaphore    semIn, semPrep, semOut;          // the three fences as timeline semaphores, or null before the first set
    uint64_t       fence[3];                        // their handles as the first CREATED ack gave them (in, prep, out)
        // The set (FEED_FG_PLACES includes the second input set's places, feed_ipc.h).
    VkImage        img[FEED_FG_PLACES];             // by the machine's order: PRESENT, GEN, HUDLESS, GEN2, PRESENT2, HUDLESS2; a place the set lacks is null
    VkDeviceMemory mem[FEED_FG_PLACES];
    HANDLE         nt[FEED_FG_PLACES];              // the ack's handles: closed with the images
    uint32_t       fmt[FEED_FG_PLACES];             // each image's DXGI format, as the ack said
    bool           storage[FEED_FG_PLACES];         // imported with storage usage (the helper made it for unordered access)
    UINT           w, h;
    bool           have;                            // the set's images are imported
        // The last ack, for the caller's line.
    int32_t        status, ffxRc, warmRc;
    uint32_t       warmUs;
    uint64_t       ctxBytes, provider;
        // Wire 3 (feed_ipc.h, FEED_CAP_FG_WIRE3), at the end:
    bool           wire3;                           // this side's last 'g' said FEED_FG_BUILD_WIRE3: its ack and the set's 'G' are wire 3's
    uint32_t       genCounts;                       // the counts of generated frames the set in hand makes (FEED_FG_GENS_*), 0 without a set
    VkImage        all[FEED_FG_PLACES];             // the set's images that exist, in the machine's order, packed: a set may lack HUDLESS and GEN2,
    uint32_t       nAll;                            //   so a barrier over "the set's images" takes these two, never img[] and a count
        // The second input set (feed_ipc.h, FEED_CAP_FG_TWIN), at the end:
    bool           twin;                            // this side's last 'g' said FEED_FG_BUILD_TWIN: its ack is FeedFgAck4Ivt (and its release's too)
    UINT           gw, gh;                          // the size of the two sets' guides, the render's (0 without them)
};
// Does the set in hand have the second input set? The import is the fact (PRESENT2 is there), as for HUDLESS.
static bool FgxTwin(const FgxSet* s) { return s->have && s->img[FEED_FG_PRESENT2] != VK_NULL_HANDLE; }

// May the pair ask for this many generated frames (FEED_FG_G_GEN_*)? Only what the helper said the set makes; a helper
// answers any other count with no frame.
static bool FgxGenOk(const FgxSet* s, uint32_t gen) { return s->have && gen < 3u && (s->genCounts & (1u << gen)) != 0; }

// What an exchange came to. CREATED, REFUSED, IMPORT and RELEASED leave the pipe in step; LATE and PIPE do not.
enum { FGX_CREATED = 1, FGX_REFUSED = 2, FGX_IMPORT = 3, FGX_RELEASED = 4, FGX_LATE = 5, FGX_PIPE = 6 };
// Under Wine and Proton: the game's own three fences and their probe, taken by FgxAcquire where its binding says Wine.
#include "fg_linux.h"

// The images given back, each with its memory and the handle the ack gave. Not the fences. The caller holds whatever lock
// its recorders need and has seen its last use of them retire.
template <class B> static void FgxDrop(FgxSet* s)
{
    FeedVk* vk = B::Vk();
    for (int k = 0; k < FEED_FG_PLACES; k++)                 // and the second input set's two
    {
        if (s->img[k]) { vk->DestroyImage(vk->dev, s->img[k], nullptr); s->img[k] = VK_NULL_HANDLE; }
        if (s->mem[k]) { vk->FreeMemory(vk->dev, s->mem[k], nullptr);   s->mem[k] = VK_NULL_HANDLE; }
        if (s->nt[k])  { CloseHandle(s->nt[k]); s->nt[k] = nullptr; }
        s->all[k] = VK_NULL_HANDLE;
    }
    s->nAll = 0; s->genCounts = 0;
    s->have = false;
}

// One 'g' and its ack. 0 when the ack was read; FGX_LATE or FGX_PIPE otherwise.
// The ack's layout follows the 'g': 124 bytes with FEED_FG_BUILD_WIRE3, 104 without (one frame a pair), both read into
// FeedFgAckIvt here. With FEED_FG_BUILD_TWIN (and WIRE3) the ack is FeedFgAck4Ivt, 156 bytes: the second set's images go
// into in2 (zero for every other 'g'; dropped where in2 is null). Under Wine, fx (the game's three fences) follows a 'g'
// with FEED_FG_BUILD_GAMEFENCES in the same write; null otherwise.
template <class B> static int FgxExchange(const FeedFgBuildIvt& b, FeedFgAckIvt* a, DWORD deadlineMs, FeedFgImageIvt* in2 = nullptr, const FeedFgFencesIvt* fx = nullptr)
{
    bool late = false;
    memset(a, 0, sizeof(*a));
    if (in2) memset(in2, 0, sizeof(FeedFgImageIvt) * (FEED_FG_PLACES - FEED_FG_IMAGES));
    if (fx)
    {
        BYTE m[sizeof(b) + sizeof(*fx)];
        memcpy(m, &b, sizeof(b)); memcpy(m + sizeof(b), fx, sizeof(*fx));
        if (!B::PipeWrite(m, sizeof(m))) return FGX_PIPE;
    }
    else if (!B::PipeWrite(&b, sizeof(b))) return FGX_PIPE;
    if ((b.flags & FEED_FG_BUILD_WIRE3) && (b.flags & FEED_FG_BUILD_TWIN))
    {
        FeedFgAck4Ivt a4 = {};
        if (!B::PipeReadWithin(&a4, sizeof(a4), deadlineMs, &late)) return late ? FGX_LATE : FGX_PIPE;
        *a = a4.a;
        if (in2) memcpy(in2, a4.in2, sizeof(a4.in2));
        return 0;
    }
    if (b.flags & FEED_FG_BUILD_WIRE3)
    {
        if (!B::PipeReadWithin(a, sizeof(*a), deadlineMs, &late)) return late ? FGX_LATE : FGX_PIPE;
        return 0;
    }
    FeedFgAck2Ivt a2 = {};
    if (!B::PipeReadWithin(&a2, sizeof(a2), deadlineMs, &late)) return late ? FGX_LATE : FGX_PIPE;
    a->status = a2.status; a->ffx_rc = a2.ffx_rc;
    for (int k = 0; k < 3; k++) a->img[k] = a2.img[k];
    a->fence_in = a2.fence_in; a->fence_prep = a2.fence_prep; a->fence_out = a2.fence_out;
    a->ctx_bytes = a2.ctx_bytes; a->provider = a2.provider; a->warm_rc = a2.warm_rc; a->warm_us = a2.warm_us;
    a->gen_counts = a2.status == FEED_FG_ST_CREATED ? FEED_FG_GENS_ONE : 0u;
    return 0;
}

// The release: the game's images go first, then the helper is told. FGX_RELEASED when the helper says it released;
// FGX_REFUSED for anything else (busy: it frees its side at the next build message; s->status says which); FGX_LATE /
// FGX_PIPE. It speaks the revision of this side's last 'g' (s->wire3), so the ack is read in the helper's layout.
template <class B> static int FgxRelease(FgxSet* s, DWORD deadlineMs)
{
    FgxDrop<B>(s);
    FeedFgBuildIvt b = {}; b.tag = 'g'; b.flags = FEED_FG_BUILD_RELEASE | (s->wire3 ? FEED_FG_BUILD_WIRE3 | (s->twin ? FEED_FG_BUILD_TWIN : 0u) : 0u);   // the second input set: its ack's layout too
    FeedFgAckIvt a;
    const int x = FgxExchange<B>(b, &a, deadlineMs);
    if (x) return x;
    s->status = a.status;
    return a.status == FEED_FG_ST_RELEASED ? FGX_RELEASED : FGX_REFUSED;
}

// 'g': ask for a set at the display's size in the presented format, with guides at the render size, and import what the
// ack carries. The game's side holds no set when this is called (a build releases before it asks).
// buildFlags: FEED_FG_BUILD_NO_HUDLESS asks for a set without the HUD-less image; FEED_FG_BUILD_WIRE3 (passed only to a
// helper that offered it, since an older helper would answer 104 bytes where 124 are read) with FEED_FG_BUILD_GEN2, the
// fourth image. The image count rule, for each place: an image the set was not asked for must come all zero and is not
// imported, or the set is refused like a failed import (a helper that made one would bind it, and the game, copying
// nothing into it, would feed AMD a stale picture). An image asked for must come and import. s->genCounts is what the
// ack says the set makes, never more than its images allow; a set without wire 3 makes one.
template <class B> static int FgxAcquire(FgxSet* s, UINT dispW, UINT dispH, DXGI_FORMAT presentFmt, UINT renderW, UINT renderH, uint32_t shape, DWORD deadlineMs, uint32_t buildFlags = 0)
{
    FgxDrop<B>(s);
    FeedFgBuildIvt b = {}; b.tag = 'g';
    b.disp_w = dispW; b.disp_h = dispH; b.present_fmt = (uint32_t)presentFmt; b.render_w = renderW; b.render_h = renderH; b.shape = shape;
    const bool w3 = (buildFlags & FEED_FG_BUILD_WIRE3) != 0;
        // The second input set: FEED_FG_BUILD_TWIN with wire 3 only, passed only to a helper that offered it: PRESENT2, and
    // HUDLESS2 where the set has HUDLESS, by the same image count rule.
    b.flags = buildFlags & (FEED_FG_BUILD_NO_HUDLESS | FEED_FG_BUILD_WIRE3 | (w3 ? FEED_FG_BUILD_GEN2 | FEED_FG_BUILD_TWIN : 0u));
    s->wire3 = w3;
    s->twin = (b.flags & FEED_FG_BUILD_TWIN) != 0;
    bool want[FEED_FG_PLACES];
    for (int k = 0; k < FEED_FG_PLACES; k++)
        want[k] = k == FEED_FG_HUDLESS ? !(b.flags & FEED_FG_BUILD_NO_HUDLESS) : k == FEED_FG_GEN2 ? (b.flags & FEED_FG_BUILD_GEN2) != 0
                : (k == FEED_FG_PRESENT2 || FEED_FG_IS_GUIDE(k)) ? s->twin : k == FEED_FG_HUDLESS2 ? (s->twin && !(b.flags & FEED_FG_BUILD_NO_HUDLESS)) : true;   // the two sets' guides come with the second set
    FeedFgAckIvt a;
    FeedFgImageIvt in2[FEED_FG_PLACES - FEED_FG_IMAGES];
        // Under Wine (fg_linux.h): the game's own fences go with the 'g', or the set is refused here with no exchange at all.
    FeedFgFencesIvt fx = {};
    const bool wine = FgxWineOf<B>::On();
    if (wine)
    {
        const int w = FgxWineBefore<B>(s, &fx);
        if (w) return w;
        b.flags |= FEED_FG_BUILD_GAMEFENCES;
    }
    const int x = FgxExchange<B>(b, &a, deadlineMs, in2, wine ? &fx : nullptr);
    if (x) return x;
    s->status = a.status; s->ffxRc = a.ffx_rc; s->warmRc = a.warm_rc; s->warmUs = a.warm_us; s->ctxBytes = a.ctx_bytes; s->provider = a.provider;
    if (wine) FgxWineOnStatus<B>(a.status);
    if (a.status != FEED_FG_ST_CREATED) return FGX_REFUSED;
    FeedVk* vk = B::Vk();
        // The handles are this process's from here on, whatever happens next: they go with the images, or just below.
    const FeedFgImageIvt* const at[FEED_FG_PLACES] = { &a.img[0], &a.img[1], &a.img[2], &a.img[3], &in2[0], &in2[1], &in2[2], &in2[3], &in2[4], &in2[5] };   // each place's word in the ack
    for (int k = 0; k < FEED_FG_PLACES; k++) s->nt[k] = (HANDLE)(uintptr_t)at[k]->handle;
    bool ok = a.fence_in && a.fence_prep && a.fence_out;
    if (!s->semIn && wine) ok = FgxWineTake<B>(s, a);         // Wine: the game's own three, held only once the probe saw both ways
    else if (ok && !s->semIn)
    {
        const VkSemaphore si = FeedVkImportFence(vk, (HANDLE)(uintptr_t)a.fence_in);
        const VkSemaphore sp = FeedVkImportFence(vk, (HANDLE)(uintptr_t)a.fence_prep);
        const VkSemaphore so = FeedVkImportFence(vk, (HANDLE)(uintptr_t)a.fence_out);
        if (si && sp && so) { s->semIn = si; s->semPrep = sp; s->semOut = so; s->fence[0] = a.fence_in; s->fence[1] = a.fence_prep; s->fence[2] = a.fence_out; }
        else
        {
            B::Line("frame generation: the helper's fences did not import as timeline semaphores (in %s, prepared %s, out %s)", si ? "ok" : "FAILED", sp ? "ok" : "FAILED", so ? "ok" : "FAILED");
            if (si) vk->DestroySemaphore(vk->dev, si, nullptr);
            if (sp) vk->DestroySemaphore(vk->dev, sp, nullptr);
            if (so) vk->DestroySemaphore(vk->dev, so, nullptr);
            ok = false;
        }
    }
    else if (ok && (a.fence_in != s->fence[0] || a.fence_prep != s->fence[1] || a.fence_out != s->fence[2]))
    {
        B::Line("frame generation: the helper's ack names other fences than its first (they live once a session): this set is refused");
        ok = false;
    }
    if (ok && !want[FEED_FG_HUDLESS] && (a.img[FEED_FG_HUDLESS].handle || a.img[FEED_FG_HUDLESS].format || a.img[FEED_FG_HUDLESS].flags))
    {
        B::Line("frame generation: the helper's ack carries a HUD-less image for a set asked without one: this set is refused");
        ok = false;
    }
    if (ok && !want[FEED_FG_GEN2] && (a.img[FEED_FG_GEN2].handle || a.img[FEED_FG_GEN2].format || a.img[FEED_FG_GEN2].flags))
    {
        B::Line("frame generation: the helper's ack carries a second generated image for a set asked without one: this set is refused");
        ok = false;
    }
        // The second input set: a place not asked must come all zero, or the set is refused, with no line of its own (the
    // caller's line says it). FgxExchange zeroes both places for every 'g' without FEED_FG_BUILD_TWIN.
    for (int k = FEED_FG_PRESENT2; k < FEED_FG_PLACES && ok; k++) if (!want[k] && (at[k]->handle || at[k]->format || at[k]->flags)) ok = false;
    for (int k = 0; k < FEED_FG_PLACES && ok; k++)
    {
        if (!want[k]) continue;
        const VkFormat vf = FeedVkFormat((DXGI_FORMAT)at[k]->format);
        s->fmt[k] = at[k]->format; s->storage[k] = (at[k]->flags & FEED_FG_IMG_UAV) != 0;
                // A guide is at the render size, in the depth's or the vectors' own format; every other place at the display's.
        const bool guide = FEED_FG_IS_GUIDE(k);
        const UINT iw = guide ? renderW : dispW, ih = guide ? renderH : dispH;
        const bool fmtOk = !guide || at[k]->format == (uint32_t)(((k - FEED_FG_GDEPTH0) & 1) ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_R32_FLOAT);
        ok = s->nt[k] != nullptr && vf != VK_FORMAT_UNDEFINED && fmtOk
          && FeedVkImportImage(vk, s->nt[k], iw, ih, vf, s->storage[k], B::HandleType(), B::Narrow(), &s->img[k], &s->mem[k]);
        if (!ok) B::Line("frame generation: image %d of the set did not import (%ux%u, DXGI format %u, %s)", k, iw, ih, at[k]->format, s->storage[k] ? "storage usage" : "sampled");
    }
    if (!ok)
    {   // give back what came, and tell the helper to free its side
                // Its own deadline, 10 s at most: the helper's release is a drain bounded at 2 s and a teardown.
        const int r = FgxRelease<B>(s, deadlineMs < 10000 ? deadlineMs : 10000);
        return (r == FGX_LATE || r == FGX_PIPE) ? r : FGX_IMPORT;
    }
    s->nAll = 0;
    for (int k = 0; k < FEED_FG_PLACES; k++) if (s->img[k]) s->all[s->nAll++] = s->img[k];   // the second input set's two last, in the machine's order
    s->genCounts = a.gen_counts & (FEED_FG_GENS_FLOW | FEED_FG_GENS_ONE | (s->img[FEED_FG_GEN2] ? FEED_FG_GENS_TWO : 0u));
    s->w = dispW; s->h = dispH; s->have = true;
    s->gw = s->twin ? renderW : 0; s->gh = s->twin ? renderH : 0;   // the guides' size, the render's
    return FGX_CREATED;
}

// =====================================================================================================================
// The per-pair half: 'Q', 'G', the one GPU wait on a helper's value, its release, and the watch's term.
// Waits across the process boundary: the helper's queue on fgIn (only for a DISPATCH 'G', whose submit was taken); the
// game's queue on fgPrep (FgxPrepWait; everything DXVK submits after it waits too), ended by the answer or by
// FgxPrepRelease from the transport's stop and the host watch; and the 'g' exchange, bounded by its deadline.
// The binding adds QueueSubmit (under the queue's lock) and Signal (vkSignalSemaphore).
// =====================================================================================================================

// What threads other than the worker read: aligned 32-bit words. Pair numbers are taken as 32 bits (eight months of one
// session at 200 frames a second).
struct FgxWaits
{
    volatile LONG lastQ;            // the n of the last 'Q' written, stored before its write: the watch reads it
    volatile LONG prepTop;          // the highest n a wait-only batch waited on, stored before its submit: the release reads it
    volatile LONG queued, refused;  // batches queued; submits of one that the queue refused
    SRWLOCK       signalLock;       // one signaller of fgPrep at a time on this side; all zero is its initial state
    uint64_t      signalledTo;      // under signalLock: the highest value this side has signalled fgPrep to
    volatile LONG waitLow;          // the lowest n a queued wait still stands for (the watch judges the helper on it): set by
                                    // the worker in FgxPrepWait; last, so no field before it moved
};

// 'Q'(n): one write of the whole message; the three values of the frame message are sent again. flags carries the reset's
// reasons as known at this write, FEED_FG_Q_PIPELINED and FEED_FG_Q_SET1 (a pair in the second set). jitterX and jitterY:
// the frame's jitter in pixels. cam: the camera's twelve floats (position, up, right, forward) or NULL; every caller
// passes NULL, so the floats are the identity and FEED_FG_Q_CAMERA stays clear. FEED_FG_Q_CAMERA is set here, by the
// pointer, never by a caller's flags.
template <class B> static bool FgxWriteQ(FgxWaits* w, uint64_t n, uint32_t flags, float frameMs, float nearPlane, float fovY, float jitterX, float jitterY, const float* cam)
{
    FeedFgQIvt q = {}; q.tag = 'Q'; q.n = n; q.flags = flags & (FEED_FG_Q_RESET_MASK | FEED_FG_Q_PIPELINED | FEED_FG_Q_SET1);
    const float ident[12] = { 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f };   // position; up y; right x; forward z
    memcpy(q.cam, cam ? cam : ident, sizeof(ident));
    if (cam) q.flags |= FEED_FG_Q_CAMERA;
    q.frame_time_ms = frameMs; q.camera_near = nearPlane; q.fov_y_rad = fovY;
    q.jitter_x = jitterX; q.jitter_y = jitterY;
    _InterlockedExchange(&w->lastQ, (LONG)n);
    return B::PipeWrite(&q, sizeof(q));
}
// 'G'(n): what became of the pair (FEED_FG_G_*), and the display's peak for the dispatch's luminance pair (0: not known).
// flags: the kind in bits 0 to 1, the reset's reasons in bits 4 to 7 (FEED_FG_G_RESET_MASK), FEED_FG_G_SET1 (the set the
// pair's 'Q' named). gen, the generated count (FEED_FG_G_GEN_*), goes in bits 2 and 3 only from this argument; a helper
// without wire 3 never reads those bits. Every caller passes nothing today, so every pair asks for one frame; a count
// must be checked with FgxGenOk first.
template <class B> static bool FgxWriteG(uint64_t n, uint32_t flags, float peak, uint32_t gen = FEED_FG_G_GEN_ONE)
{
    FeedFgGIvt g = {}; g.tag = 'G'; g.n = n; g.flags = (flags & (FEED_FG_G_KIND_MASK | FEED_FG_G_RESET_MASK | FEED_FG_G_SET1)) | ((gen << FEED_FG_G_GEN_SHIFT) & FEED_FG_G_GEN_MASK); g.peak = peak;
    return B::PipeWrite(&g, sizeof(g));
}

// The one GPU wait: a batch with no command buffer that waits for fgPrep to reach n, on the device's own queue. Everything
// submitted after it begins behind it: that is its purpose (the next injection's body writes the guides prepare(n) reads)
// and its danger. The top is stored first, so a release between the store and the submit still signals past it.
// False: no fence yet, or the queue refused the batch (counted; nothing waits then).
template <class B> static bool FgxPrepWait(FgxSet* s, FgxWaits* w, uint64_t n)
{
    if (!s->semPrep || !n) return false;
    if ((LONG)n > w->prepTop) _InterlockedExchange(&w->prepTop, (LONG)n);
        // The lowest value a queued wait still stands for: this one's, unless an earlier wait is still unanswered. Only the
    // worker queues these waits, so this read and store race no other writer.
    {
        const LONG low = w->waitLow;
        if (!low || FeedVkTimelineValue(B::Vk(), s->semPrep) >= (uint64_t)(ULONG)low) _InterlockedExchange(&w->waitLow, (LONG)n);
    }
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.waitSemaphoreValueCount = 1; tl.pWaitSemaphoreValues = &n;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &s->semPrep; si.pWaitDstStageMask = &stage;
    if (B::QueueSubmit(&si) != VK_SUCCESS) { _InterlockedIncrement(&w->refused); return false; }
    _InterlockedIncrement(&w->queued);
    return true;
}

// The release: fgPrep signalled from the CPU to one past the top, so no batch of the game's can still be behind it. No wait
// first: the waiter is the game's own queue. Only for a helper that is gone, going or condemned: while it lives the helper
// is this fence's GPU signaller. Two callers (the stop, the watch) may come at once: one lock, and no value is signalled
// twice. A fence already at or past the top is left alone: a timeline is only signalled upward. *did: this call signalled.
template <class B> static VkResult FgxPrepRelease(FgxSet* s, FgxWaits* w, bool* did)
{
    *did = false;
    const uint64_t top = (uint64_t)(ULONG)w->prepTop;
    if (!s->semPrep || !top) return VK_SUCCESS;              // nothing of the game's ever waited on it
    VkResult r = VK_SUCCESS;
    AcquireSRWLockExclusive(&w->signalLock);
    if (top + 1 > w->signalledTo && FeedVkTimelineValue(B::Vk(), s->semPrep) < top)
    {
        r = B::Signal(s->semPrep, top + 1);
        if (r == VK_SUCCESS) w->signalledTo = top + 1;
        *did = true;
    }
    ReleaseSRWLockExclusive(&w->signalLock);
    return r;
}

// The watch's term for a prepare, pure. A helper hung in a prepare is never behind with a frame (the game's next injection
// waits behind fgPrep), so the transport's watch needs this extra look, twice a second.
// waitLow: the lowest n a queued wait still stands for; while fgPrep is below it the helper is judged on it, not on
// lastQ, because frame n+1's input sits behind the wait for n. inRetired: the in-fence as read here (0: unreadable);
// while it is below the pair judged the helper is waiting for the game. now100: 100 ns units, still while asleep.
// True: fgPrep has stood below the judged pair, not moving, for FGX_WATCH_MS, with that frame's input retired.
struct FgxWatchSt { uint64_t seen1, since; };               // fgPrep at the last look plus one (0: none), and when it was first seen there
enum { FGX_WATCH_MS = 4000 };
static bool FgxWatchStep(FgxWatchSt* st, uint64_t lastQ, uint64_t waitLow, uint64_t prep, uint64_t inRetired, uint64_t now100)
{
    if (!lastQ || prep >= lastQ) { st->seen1 = 0; return false; }                    // every 'Q' written has had its prepare
    const uint64_t need = (waitLow && prep < waitLow) ? waitLow : lastQ;             // the pair the helper is judged on
    if (inRetired && inRetired < need) { st->seen1 = 0; return false; }              // the prepare waits for the game's own input
    if (prep + 1 != st->seen1) { st->seen1 = prep + 1; st->since = now100; return false; }   // behind, but it moved (or this is the first look)
    return now100 - st->since >= (uint64_t)FGX_WATCH_MS * 10000ull;
}

// =====================================================================================================================
// The helper's place on the card (mend 2 in fg_machine_armed.h; tools/test-fg-pipe runs these lines).
// Windows' GPU scheduler gives the foreground program's band priority and the band under it about a tenth of the card,
// so the helper's dispatch waits for the game's queue to run dry. The helper's process is raised to the top class with
// gdi32's D3DKMTSetProcessSchedulingPriorityClass, found by name (absent on Wine or old Windows: then nothing changes;
// HIGH, 4, does not help). Works unelevated; given back where the engage ends. Held only while a pair rides: a streaming
// host reports NVIDIA encoder freezes with this class held for a whole session.
// =====================================================================================================================
enum { FGX_CARD_NORMAL = 2, FGX_CARD_TOP = 5 };
struct FgxCard
{
    volatile LONG asks, took, refused, absent;   // times the top class was asked for; it took (set, and read back); it did not; gdi32 has no such call
    volatile LONG status, read, prior;           // the last ask's status; the class read back (-1: not readable); the class before the first ask plus one (0: never read)
    volatile LONG backs, backStatus;             // times it was given back, and the last one's status
};
typedef LONG (WINAPI* FgxKmtSetClass_t)(HANDLE, int);
typedef LONG (WINAPI* FgxKmtGetClass_t)(HANDLE, int*);
// up: ask for the top class; false: give back what the process had. proc: a handle of the helper's with the right to set
// its information. Any thread; two at once cost a call each and leave what the later one asked.
static void FgxCardClass(HANDLE proc, bool up, FgxCard* c)
{
    if (up) _InterlockedIncrement(&c->asks);
    const HMODULE gdi = GetModuleHandleA("gdi32.dll");
    const FgxKmtSetClass_t set = gdi ? (FgxKmtSetClass_t)GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass") : nullptr;
    const FgxKmtGetClass_t get = gdi ? (FgxKmtGetClass_t)GetProcAddress(gdi, "D3DKMTGetProcessSchedulingPriorityClass") : nullptr;
    if (!proc || !set) { if (up) _InterlockedIncrement(&c->absent); return; }
    int got = -1;
    if (up)
    {
        if (!c->prior) { int was = -1; _InterlockedExchange(&c->prior, (get && get(proc, &was) == 0 && was >= 0 && was <= FGX_CARD_TOP) ? (LONG)was + 1 : FGX_CARD_NORMAL + 1); }
        const LONG st = set(proc, FGX_CARD_TOP);
        const bool readOk = get && get(proc, &got) == 0;
        _InterlockedExchange(&c->status, st); _InterlockedExchange(&c->read, readOk ? (LONG)got : -1);
        if (st == 0 && (!readOk || got == FGX_CARD_TOP)) _InterlockedIncrement(&c->took); else _InterlockedIncrement(&c->refused);
        return;
    }
    const LONG prior = c->prior ? c->prior - 1 : FGX_CARD_NORMAL;
    _InterlockedExchange(&c->backStatus, set(proc, (int)prior));
    _InterlockedIncrement(&c->backs);
}

// =====================================================================================================================
// The order's own fence (mend 3 in fg_machine_armed.h; tools/test-fg-pipe runs these lines).
// A batch with no command buffer that waits for a timeline of the game side's own to reach a pair's number, so the next
// frame's work leaves the card to the helper's dispatch. Only this side signals it, from the CPU, when the machine says;
// the machine keeps at most one present behind it (ArmedOrdBeforePresent). Made once a session, never destroyed; one
// signaller at a time, only upward. submit(si): the caller already holds the queue's lock (the wait is queued inside the
// hand's span).
// =====================================================================================================================
struct FgxOrd
{
    VkSemaphore   sem;              // the fence (null: none could be made, and no wait is ever queued)
    SRWLOCK       lock;             // one signaller at a time; all zero is its initial state
    uint64_t      to;               // under the lock: the value it was last signalled to
    volatile LONG queued, refused;  // waits the queue took; ones it did not
    volatile LONG signals, failed;  // signals made; ones Vulkan refused
};
template <class B> static bool FgxOrdMake(FgxOrd* o)
{
    if (o->sem) return true;
    HANDLE none = nullptr;
    return FeedVkCreateTimeline(B::Vk(), false, &o->sem, &none) >= 0 && o->sem != VK_NULL_HANDLE;
}
static bool FgxOrdWait(FgxOrd* o, uint64_t n, VkResult (*submit)(const VkSubmitInfo*))
{
    if (!o->sem || !n) return false;
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.waitSemaphoreValueCount = 1; tl.pWaitSemaphoreValues = &n;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &o->sem; si.pWaitDstStageMask = &stage;
    if (submit(&si) != VK_SUCCESS) { _InterlockedIncrement(&o->refused); return false; }
    _InterlockedIncrement(&o->queued);
    return true;
}
template <class B> static void FgxOrdSignal(FgxOrd* o, uint64_t n)
{
    if (!o->sem) return;
    AcquireSRWLockExclusive(&o->lock);
    if (n > o->to)
    {
        if (B::Signal(o->sem, n) == VK_SUCCESS) { o->to = n; _InterlockedIncrement(&o->signals); }
        else _InterlockedIncrement(&o->failed);
    }
    ReleaseSRWLockExclusive(&o->lock);
}

// =====================================================================================================================
// The gate before DXVK's Present lives in source/transport/present_gate.h (IvtGateCore, one state ivtGate). Before the
// machine is installed the thunk in front of DXVK's Present calls it (out-fence only); after, the machine's WorkerPresent
// (FgP::PresentGate, plus fgPrep), and DxvkBare takes the thunk out of the chain.
// Why: with vsync on, two presents issued behind a queue stalled on the helper are never reported complete by NVIDIA's
// driver; DXVK waits for that with no time-out inside its device lock and the game freezes for good. So each present first
// waits on the CPU until the helper has reached what batches queued before the previous present wait for: at most one
// present stands behind an outstanding wait. The cap (past the watch's worst case) and a failed counter read let a
// present go. A counter of 0 is a value, not a failed read (the fences start at 0).
// =====================================================================================================================
