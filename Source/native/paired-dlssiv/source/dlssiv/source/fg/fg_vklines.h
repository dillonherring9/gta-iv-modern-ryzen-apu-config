// fg_vklines.h: frame generation's Vulkan lines over a binding V, shared by the product (FgPA) and the desk tool
// tools\test-fg-vklines. They provide copies into and out of the back buffer, the game-side images SAVE (what a fire wrote
// over) and HOLD (the real frame while its generated partner is shown), the set's first layout, the command ring, and the
// three spans the machine records between QLock and QUnlock. Every copy says whether it was recorded.
// Rules: no D3D9 call and no lock here (the caller holds the locks and opens DT_DIRECT; FgvBbQuery is the one DXVK call).
// The back buffer's image and layout come from a fresh query every time: DXVK swaps the image at every Present. Helper
// images are acquired from VK_QUEUE_FAMILY_EXTERNAL before a copy and released after, in the same buffer. A span that
// cannot record one copy records nothing. Core Vulkan only (timeline semaphores, 1.2).
// The binding V, all static:
// FeedVk*  Vk()                                                the device's loaded functions (transport/feed_vk.h)
// VkResult CbReset(VkCommandBuffer)                            vkResetCommandBuffer
// VkResult CbBegin(VkCommandBuffer, const VkCommandBufferBeginInfo*)
// VkResult CbEnd(VkCommandBuffer)
// VkResult Submit(const VkSubmitInfo*)                         one vkQueueSubmit; the caller holds the submission lock
// LONGLONG Now(), Qpf()                                        the clock, for the ring's 2 s bound
// Included after transport/feed_vk.h and dxvk/dxvk_interop_min.h (FgvBbQuery names ID3D9VkInteropTexture).
#pragma once
#ifndef FEED_VK_MAX_GROUP
#error "fg_vklines.h needs transport/feed_vk.h before it"
#endif

// The game's access to its back buffer, as the transport scopes it (DlssIvTransport.h, kIvtGameWriteStages and the three
// beside it), under this file's own names because the transport's header is the mod's alone. The product puts
// FGV_ASSERT_TRANSPORT_SCOPES() beside its include; the desk tool's run.py compares both definitions.
#define FGV_GAME_WRITE_STAGES (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT)
#define FGV_GAME_WRITE_ACCESS (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)
#define FGV_GAME_USE_STAGES   (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT)
#define FGV_GAME_USE_ACCESS   (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)
#define FGV_ASSERT_TRANSPORT_SCOPES() \
    static_assert(FGV_GAME_WRITE_STAGES == kIvtGameWriteStages && FGV_GAME_WRITE_ACCESS == kIvtGameWriteAccess \
               && FGV_GAME_USE_STAGES == kIvtGameUseStages && FGV_GAME_USE_ACCESS == kIvtGameUseAccess, \
                  "fg_vklines.h scopes the back buffer's barriers as the transport does (DlssIvTransport.h)")

// The ring's depth. A shown pair records up to eight buffers (the hand 1, I's write, marker and restore 3, N's write and
// restore 2, two to spare for a HUD-less span).
enum { FGV_RING = 32 };

// What a span came to.
enum { FGV_SUBMITTED = 1, FGV_NOCOPY = 0, FGV_REFUSED = -1 };

struct FgvRing
{
    VkCommandBuffer cb[FGV_RING];
    UINT64          cbValue[FGV_RING];  // the value of semLocal that retires each slot's last submit (0: none)
    VkSemaphore     semLocal;           // this ring's own timeline
    int             next, resvN, resvUsed, curSlot;
    VkCommandBuffer cur;
    VkSemaphore     pendSem; UINT64 pendVal;   // a signal the next submit carries (fgIn), set by the caller
    volatile LONGLONG askedN, tookN;    // the last value asked of a submit on semLocal, and the last a submit took
    LONG            slotWaited, overruns, submitFails, noCopy;
};

// One image of the game's own (SAVE, HOLD) with its memory.
struct FgvImage { VkImage img; VkDeviceMemory mem; VkDeviceSize bytes; };

// A fresh query of a surface's Vulkan image: DXVK swaps the image behind the back buffer's surface at every Present, so it
// is never cached. The caller has opened DT_DIRECT and holds no submission lock.
static bool FgvBbQuery(IDirect3DSurface9* surf, VkImage* img, VkImageLayout* layout, UINT* w, UINT* h, VkFormat* fmt)
{
    if (!surf) return false;
    ID3D9VkInteropTexture* it = nullptr;
    if (FAILED(surf->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) || !it) return false;
    VkImage i = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };   // sType must be set (IvtLiveBackbuffer says why)
    const HRESULT hr = it->GetVulkanImageInfo(&i, &l, &info);
    it->Release();
    if (FAILED(hr) || !i) return false;
    *img = i; *layout = l; *w = info.extent.width; *h = info.extent.height; *fmt = info.format;
    return true;
}

// The back buffer's barriers around a copy, scoped as the transport's are: the game writes it through colour-attachment
// output and transfer, and later draws, samplers and DXVK's present blit use it. into: before the copy; write: the copy
// writes the back buffer (its first scope is then everything that used the image, not only what wrote it).
static void FgvBbBarrier(FeedVk* vk, VkCommandBuffer cb, VkImage bb, VkImageLayout from, VkImageLayout to, bool into, bool write)
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    const VkAccessFlags xfer = write ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
    b.srcAccessMask = into ? (FGV_GAME_WRITE_ACCESS | (write ? FGV_GAME_USE_ACCESS : 0)) : xfer;
    b.dstAccessMask = into ? xfer : FGV_GAME_USE_ACCESS;
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = bb; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    const VkPipelineStageFlags src = into ? (FGV_GAME_WRITE_STAGES | (write ? FGV_GAME_USE_STAGES : 0)) : VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkPipelineStageFlags dst = into ? VK_PIPELINE_STAGE_TRANSFER_BIT : FGV_GAME_USE_STAGES;
    vk->CmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// The copy out: the back buffer into an image (GENERAL). external: the image is one of the helper's set, acquired before
// the copy and released after it, in the same buffer. False, nothing recorded: no back buffer or no image.
static bool FgvRecCopyOut(FeedVk* vk, VkCommandBuffer cb, VkImage bb, VkImageLayout bbLayout, VkImage dst, bool external, uint32_t family, UINT w, UINT h)
{
    if (!bb || !dst) return false;
    const VkImageLayout l = bbLayout;
    const VkImageLayout sl = l == VK_IMAGE_LAYOUT_GENERAL ? l : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    FgvBbBarrier(vk, cb, bb, l, sl, true, false);
    if (external) FeedVkExternalTransferN(vk, cb, &dst, 1, family, false /*acquire*/);
    FeedVkCopyImage(vk, cb, bb, sl, dst, VK_IMAGE_LAYOUT_GENERAL, w, h);
    if (external) FeedVkExternalTransferN(vk, cb, &dst, 1, family, true /*release*/);
    FgvBbBarrier(vk, cb, bb, sl, l, false, false);
    return true;
}
// The copy in: an image (GENERAL) into the back buffer, raw, with the same bracket for a helper's image.
// False, nothing recorded: no back buffer or no image.
static bool FgvRecCopyIn(FeedVk* vk, VkCommandBuffer cb, VkImage src, bool external, uint32_t family, VkImage bb, VkImageLayout bbLayout, UINT w, UINT h)
{
    if (!bb || !src) return false;
    const VkImageLayout l = bbLayout;
    const VkImageLayout dl = l == VK_IMAGE_LAYOUT_GENERAL ? l : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    FgvBbBarrier(vk, cb, bb, l, dl, true, true);
    if (external) FeedVkExternalTransferN(vk, cb, &src, 1, family, false /*acquire*/);
    FeedVkCopyImage(vk, cb, src, VK_IMAGE_LAYOUT_GENERAL, bb, dl, w, h);
    if (external) FeedVkExternalTransferN(vk, cb, &src, 1, family, true /*release*/);
    FgvBbBarrier(vk, cb, bb, dl, l, false, true);
    return true;
}

// ---- the images of the game's own ----
// One image at the back buffer's size and format: OPTIMAL, device-local, dedicated, TRANSFER_SRC and DST, never exported.
// It costs video memory but no streaming budget: DXVK's own counter does not see a raw Vulkan image.
// mp: vkGetPhysicalDeviceMemoryProperties of the device's adapter, read once by the caller.
static bool FgvImageMake(FeedVk* vk, const VkPhysicalDeviceMemoryProperties& mp, UINT w, UINT h, VkFormat fmt, FgvImage* out)
{
    out->img = VK_NULL_HANDLE; out->mem = VK_NULL_HANDLE; out->bytes = 0;
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D; ici.format = fmt; ici.extent = { w, h, 1 };
    ici.mipLevels = 1; ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT; ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img = VK_NULL_HANDLE;
    if (vk->CreateImage(vk->dev, &ici, nullptr, &img) != VK_SUCCESS) return false;
    VkMemoryRequirements req = {};
    vk->GetImageMemoryRequirements(vk->dev, img, &req);
    uint32_t type = UINT32_MAX;
    for (uint32_t t = 0; t < mp.memoryTypeCount && t < 32; t++)
        if ((req.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { type = t; break; }
    if (type == UINT32_MAX) { vk->DestroyImage(vk->dev, img, nullptr); return false; }
    VkMemoryDedicatedAllocateInfo ded = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    ded.image = img;
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.pNext = &ded;
    mai.allocationSize = req.size; mai.memoryTypeIndex = type;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vk->AllocateMemory(vk->dev, &mai, nullptr, &mem) != VK_SUCCESS) { vk->DestroyImage(vk->dev, img, nullptr); return false; }
    if (vk->BindImageMemory(vk->dev, img, mem, 0) != VK_SUCCESS)
    { vk->FreeMemory(vk->dev, mem, nullptr); vk->DestroyImage(vk->dev, img, nullptr); return false; }
    out->img = img; out->mem = mem; out->bytes = req.size;
    return true;
}
// The caller has seen the ring retire first (FgvRetireWait): a fire's buffers name the image.
static void FgvImageFree(FeedVk* vk, FgvImage* im)
{
    if (im->img) vk->DestroyImage(vk->dev, im->img, nullptr);
    if (im->mem) vk->FreeMemory(vk->dev, im->mem, nullptr);
    im->img = VK_NULL_HANDLE; im->mem = VK_NULL_HANDLE; im->bytes = 0;
}
// The first layout, in a buffer submitted alone that retires before anything is copied (a change from UNDEFINED may
// discard contents): every image UNDEFINED to GENERAL, then the set's images released to EXTERNAL, so every later buffer
// that touches one acquires it after a release. own: SAVE and HOLD (either may be absent). set: the helper's images.
static void FgvRecFirstLayout(FeedVk* vk, VkCommandBuffer cb, const VkImage* own, uint32_t nOwn, const VkImage* set, uint32_t nSet, uint32_t family)
{
    VkImage all[FEED_VK_MAX_GROUP]; uint32_t na = 0;
    for (uint32_t i = 0; i < nOwn && na < FEED_VK_MAX_GROUP; i++) if (own[i]) all[na++] = own[i];
    const uint32_t firstSet = na;
    for (uint32_t i = 0; i < nSet && na < FEED_VK_MAX_GROUP; i++) if (set[i]) all[na++] = set[i];
    FeedVkBarrierN(vk, cb, all, na, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    FeedVkExternalTransferN(vk, cb, all + firstSet, na - firstSet, family, true /*release*/);
}

// ---- the command ring ----
// FgvCbTake: the next n slots must have retired, within 2 s together; a slot that has to be waited for is counted, and
// one that does not retire makes the span withhold, before any lock.
template <class V> static bool FgvCbTake(FgvRing& v, int n)
{
    if (n > 4) n = 4;
    const LONGLONG t0 = V::Now();
    for (int i = 0; i < n; i++)
    {
        const int slot = (v.next + i) % FGV_RING;
        const UINT64 want = v.cbValue[slot];
        if (!want || FeedVkTimelineValue(V::Vk(), v.semLocal) >= want) continue;
        DEVSTAT(v.slotWaited++;)
        const LONGLONG leftMs = 2000 - (V::Now() - t0) * 1000 / V::Qpf();
        if (leftMs <= 0 || !FeedVkWaitTimeline(V::Vk(), v.semLocal, want, (uint32_t)leftMs)) { v.resvN = 0; return false; }
    }
    v.resvN = n; v.resvUsed = 0;
    return true;
}
// FgvRecBegin: the next reserved slot, reset and begun, with a global memory barrier at its head (DXVK's earlier work on the
// queue is visible to the copy that follows). A buffer past the reservation is the caller's fault, counted, and waited for.
template <class V> static void FgvRecBegin(FgvRing& v)
{
    const int slot = v.next;
    if (v.resvUsed >= v.resvN)
    {
        DEVSTAT(v.overruns++;)
        if (v.cbValue[slot]) FeedVkWaitTimeline(V::Vk(), v.semLocal, v.cbValue[slot], 2000);
    }
    v.resvUsed++;
    v.curSlot = slot; v.cur = v.cb[slot];
    V::CbReset(v.cur);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    V::CbBegin(v.cur, &bi);
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    V::Vk()->CmdPipelineBarrier(v.cur, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}
// FgvRecSubmit: under the submission lock the caller holds. Its own value on semLocal retires the slot. The VkResult is
// the caller's to act on: a refused submit ends frame generation (it never stops the transport).
template <class V> static VkResult FgvRecSubmit(FgvRing& v)
{
    V::CbEnd(v.cur);
    const UINT64 val = (UINT64)InterlockedIncrement64(&v.askedN);
    VkSemaphore sems[2] = { v.semLocal, v.pendSem }; UINT64 vals[2] = { val, v.pendVal };
    const uint32_t ns = v.pendSem != VK_NULL_HANDLE ? 2u : 1u;
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.signalSemaphoreValueCount = ns; tl.pSignalSemaphoreValues = vals;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl; si.commandBufferCount = 1; si.pCommandBuffers = &v.cur; si.signalSemaphoreCount = ns; si.pSignalSemaphores = sems;
    const VkResult r = V::Submit(&si);
    v.cbValue[v.curSlot] = r == VK_SUCCESS ? val : 0;            // a refused submit's value is never signalled: nothing may wait for it
    if (r == VK_SUCCESS) InterlockedExchange64(&v.tookN, (LONGLONG)val);
    v.pendSem = VK_NULL_HANDLE; v.pendVal = 0;
    v.next = (v.curSlot + 1) % FGV_RING;
    DEVSTAT(if (r != VK_SUCCESS) v.submitFails++;)
    return r;
}
// The retire wait (a Reset, a build's head, before SAVE or HOLD is freed): every submit the queue took has retired, or ms
// passed.
template <class V> static bool FgvRetireWait(FgvRing& v, DWORD ms)
{
    const UINT64 last = (UINT64)InterlockedCompareExchange64(&v.tookN, 0, 0);
    return !last || v.semLocal == VK_NULL_HANDLE || FeedVkWaitTimeline(V::Vk(), v.semLocal, last, ms);
}

// ---- the three spans ----
// Each runs with the buffers taken (FgvCbTake), the device lock held, a flush, a fresh back-buffer query and the
// submission lock held. FGV_SUBMITTED: recorded and taken. FGV_NOCOPY: nothing begun, the ring unmoved. FGV_REFUSED: the
// queue refused it (frame generation ends).
// The hand: the finished real frame into the helper's PRESENT (and HUDLESS) and, with wantHold, into HOLD. The caller sets
// fgIn only when PRESENT is passed. present NULL (the helper may still read PRESENT, or no source): with HOLD the frame is
// still stored; without, FGV_NOCOPY and the caller presents inline. With wantHold a missing HOLD is always FGV_NOCOPY.
template <class V> static int FgvSpanHand(FgvRing& v, VkImage bb, VkImageLayout bbLayout, VkImage present, VkImage hudless, VkImage hold, bool wantHold, uint32_t family, UINT w, UINT h)
{
    if (!bb || (wantHold && !hold) || (!wantHold && !present)) { DEVSTAT(v.noCopy++;) return FGV_NOCOPY; }
    FgvRecBegin<V>(v);
    if (present) FgvRecCopyOut(V::Vk(), v.cur, bb, bbLayout, present, true, family, w, h);
    if (present && hudless) FgvRecCopyOut(V::Vk(), v.cur, bb, bbLayout, hudless, true, family, w, h);
    if (wantHold) FgvRecCopyOut(V::Vk(), v.cur, bb, bbLayout, hold, false, family, w, h);
    return FgvRecSubmit<V>(v) == VK_SUCCESS ? FGV_SUBMITTED : FGV_REFUSED;
}
// The write of a fire, in one buffer: the back buffer as the game has it now into SAVE (no dirty test), then the picture
// into the back buffer. src: the generated image (srcExternal true) for a generated frame; for the real frame HOLD (own
// form) or the helper's PRESENT (shared form, srcExternal true). The save is recorded first: taken after the write it
// would hold the generated picture, and the restore would leave it in the game's image.
template <class V> static int FgvSpanWrite(FgvRing& v, VkImage bb, VkImageLayout bbLayout, VkImage save, VkImage src, bool srcExternal, uint32_t family, UINT w, UINT h)
{
    if (!bb || !save || !src) { DEVSTAT(v.noCopy++;) return FGV_NOCOPY; }
    FgvRecBegin<V>(v);
    FgvRecCopyOut(V::Vk(), v.cur, bb, bbLayout, save, false, family, w, h);
    FgvRecCopyIn(V::Vk(), v.cur, src, srcExternal, family, bb, bbLayout, w, h);
    return FgvRecSubmit<V>(v) == VK_SUCCESS ? FGV_SUBMITTED : FGV_REFUSED;
}
// The restore of a fire: SAVE into the back buffer image current after the Present (a fresh query: with more than one
// image DXVK has rotated, and that image is where the game goes on drawing). With one image the caller has flushed DXVK's
// stream first: the present's blit reads the image this buffer overwrites.
template <class V> static int FgvSpanRestore(FgvRing& v, VkImage save, VkImage bbAfter, VkImageLayout bbLayout, uint32_t family, UINT w, UINT h)
{
    if (!bbAfter || !save) { DEVSTAT(v.noCopy++;) return FGV_NOCOPY; }
    FgvRecBegin<V>(v);
    FgvRecCopyIn(V::Vk(), v.cur, save, false, family, bbAfter, bbLayout, w, h);
    return FgvRecSubmit<V>(v) == VK_SUCCESS ? FGV_SUBMITTED : FGV_REFUSED;
}

// ------------------------------------------------------------------------------------------------- the spans on the card
// Two GPU timestamps round every ring buffer (S1: the span's own GPU time). The tail stamp of a fire's write is where its
// picture reaches the back buffer, with DXVK's blit next, so a pair's write spacing is what reaches the screen (S2).
// A slot's stamps are read when the slot comes round; one not yet available is counted lost, never waited for. A queue
// with no timestamps (timestampValidBits 0) makes no pool.
enum { FGV_TC_TO_PRESENT = 1, FGV_TC_TO_HUDLESS = 2, FGV_TC_TO_OWN = 4, FGV_TC_FROM_GEN = 8, FGV_TC_FROM_STORE = 16, FGV_TC_FROM_SAVE = 32 };
// What a buffer was, by what it copied: the hand's span, a generated frame's write, a real frame's write (out of its store,
// never with a copy into HUDLESS), and every other buffer.
enum { FGV_TS_NONE = 0, FGV_TS_HAND = 1, FGV_TS_GEN = 2, FGV_TS_REAL = 3, FGV_TS_OTHER = 4, FGV_TS_KINDS = 5 };
static int FgvTsKindOf(DWORD c)
{
    if (c & FGV_TC_FROM_GEN) return FGV_TS_GEN;
    if ((c & FGV_TC_FROM_STORE) && !(c & FGV_TC_TO_HUDLESS)) return FGV_TS_REAL;
    if ((c & FGV_TC_TO_PRESENT) && !(c & (FGV_TC_FROM_STORE | FGV_TC_FROM_SAVE))) return FGV_TS_HAND;
    return c ? FGV_TS_OTHER : FGV_TS_NONE;
}
struct FgvTs
{
    VkQueryPool pool;                   // two a ring slot; VK_NULL_HANDLE: not made (every reading is "not read")
    double      ns;                     // a tick, in nanoseconds (VkPhysicalDeviceLimits::timestampPeriod)
    UINT64      mask;                   // the queue's valid bits
    PFN_vkCmdResetQueryPool   CmdReset;
    PFN_vkCmdWriteTimestamp   CmdWrite;
    PFN_vkGetQueryPoolResults Results;
    DWORD copies;                       // what the buffer being recorded copies (FGV_TC_*)
    int   kind[FGV_RING];               // per slot: what its last submit was (FGV_TS_*; NONE: no stamps in it)
    LONG  frame[FGV_RING];              // and the frame its fire was for (the caller's number)
    LONG  lost;                         // a slot came round again with its stamps not available
};
// The pool, once; ns and validBits are the caller's reading of the device. False and no pool: the stamps are not read.
static bool FgvTsMake(FgvTs* t, FeedVk* vk, double ns, uint32_t validBits)
{
    memset(t, 0, sizeof(*t));
    if (!vk || !vk->GetDeviceProcAddr || ns <= 0.0 || validBits == 0) return false;
    PFN_vkCreateQueryPool create = (PFN_vkCreateQueryPool)vk->GetDeviceProcAddr(vk->dev, "vkCreateQueryPool");
    t->CmdReset = (PFN_vkCmdResetQueryPool)vk->GetDeviceProcAddr(vk->dev, "vkCmdResetQueryPool");
    t->CmdWrite = (PFN_vkCmdWriteTimestamp)vk->GetDeviceProcAddr(vk->dev, "vkCmdWriteTimestamp");
    t->Results = (PFN_vkGetQueryPoolResults)vk->GetDeviceProcAddr(vk->dev, "vkGetQueryPoolResults");
    if (!create || !t->CmdReset || !t->CmdWrite || !t->Results) return false;
    VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2 * FGV_RING;
    if (create(vk->dev, &qi, nullptr, &t->pool) != VK_SUCCESS) { t->pool = VK_NULL_HANDLE; return false; }
    t->ns = ns; t->mask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);
    return true;
}
// One slot's last two stamps: true with the head and tail ticks, its kind and frame; false where the slot held none or
// they are not available (counted lost). Read once: the slot is marked empty.
static bool FgvTsRead(FgvTs& t, FeedVk* vk, int slot, int* kind, LONG* frame, UINT64* head, UINT64* tail)
{
    if (!t.pool || slot < 0 || slot >= FGV_RING || t.kind[slot] == FGV_TS_NONE) return false;
    *kind = t.kind[slot]; *frame = t.frame[slot]; t.kind[slot] = FGV_TS_NONE;
    UINT64 r[4] = {};                   // (value, availability) twice
    const VkResult q = t.Results(vk->dev, t.pool, (uint32_t)(2 * slot), 2, sizeof(r), r, 2 * sizeof(UINT64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((q != VK_SUCCESS && q != VK_NOT_READY) || !r[1] || !r[3]) { t.lost++; return false; }
    *head = r[0] & t.mask; *tail = r[2] & t.mask;
    return true;
}
// A tick difference in microseconds, on the queue's valid bits (a wrap is taken once).
static double FgvTsUs(const FgvTs& t, UINT64 from, UINT64 to) { return (double)((to - from) & t.mask) * t.ns / 1000.0; }
// The head of the buffer just begun (behind FgvRecBegin): the two queries reset, the head stamp.
static void FgvTsHead(FgvTs& t, FgvRing& v)
{
    t.copies = 0;
    if (!t.pool) return;
    t.CmdReset(v.cur, t.pool, (uint32_t)(2 * v.curSlot), 2);
    t.CmdWrite(v.cur, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, t.pool, (uint32_t)(2 * v.curSlot));
}
// The tail of the buffer about to be submitted (ahead of FgvRecSubmit): the tail stamp, and what the slot holds now.
static void FgvTsTail(FgvTs& t, FgvRing& v, LONG frame)
{
    if (!t.pool) return;
    t.CmdWrite(v.cur, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, t.pool, (uint32_t)(2 * v.curSlot + 1));
    t.kind[v.curSlot] = FgvTsKindOf(t.copies); t.frame[v.curSlot] = frame;
    if (t.kind[v.curSlot] == FGV_TS_NONE) t.kind[v.curSlot] = FGV_TS_OTHER;
}
// A submit the queue did not take leaves its slot's stamps unwritten: nothing is to be read of it.
static void FgvTsRefused(FgvTs& t, int slot) { if (slot >= 0 && slot < FGV_RING) t.kind[slot] = FGV_TS_NONE; }
// S2, the pairs on the card's clock, from the writes' tail stamps in ring (submission) order. For the real frame of frame
// f: D1, its generated frame's write to its own; D2, the real frame of f - 1 to this pair's generated frame; and the
// generated frame's place between its two real frames, per mille of their spacing (500: the middle). Frame numbers are
// the caller's, consecutive frames consecutive numbers.
enum { FGV_TSP_RING = 64 };
struct FgvTsPace { LONG genF[FGV_TSP_RING]; UINT64 genT[FGV_TSP_RING]; LONG lastNF; UINT64 lastNT; bool haveN; };
// False: nothing to say (not a real frame's write, or no generated frame before it). Else d1Us, and where the previous
// real frame is in, d2Us and placePm too (*both true).
static bool FgvTsPaceTake(FgvTsPace& p, const FgvTs& t, int kind, LONG frame, UINT64 tail, double* d1Us, double* d2Us, LONG* placePm, bool* both)
{
    *both = false;
    const int i = (int)((ULONG)frame & (FGV_TSP_RING - 1));
    if (kind == FGV_TS_GEN) { p.genF[i] = frame; p.genT[i] = tail; return false; }
    if (kind != FGV_TS_REAL) return false;
    const bool gen = p.genF[i] == frame && p.genT[i] != 0;
    const bool prev = p.haveN && p.lastNF == frame - 1;
    bool said = false;
    if (gen)
    {
        *d1Us = FgvTsUs(t, p.genT[i], tail); said = true;
        if (prev)
        {
            *d2Us = FgvTsUs(t, p.lastNT, p.genT[i]);
            const double s = *d1Us + *d2Us;
            *placePm = s > 0.0 ? (LONG)(*d2Us * 1000.0 / s + 0.5) : 0;
            *both = true;
        }
        p.genT[i] = 0;
    }
    p.lastNF = frame; p.lastNT = tail; p.haveN = true;
    return said;
}
