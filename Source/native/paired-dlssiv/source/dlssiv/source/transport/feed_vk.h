// From DLSS5-Feeder v0.12.0 (https://github.com/jlrouzies-fr/DLSS5-Feeder), adapted.
// Copyright (c) 2026 Jean-Laurent ROUZIES. MIT License: see LICENSE-DLSS5-Feeder.txt
// in this folder. Used by DLSS-IV as the resolve host's protocol / interop.
//
// DIVERGENCE FROM UPSTREAM, written down rather than left to a diff. Until 2026-09-09
// this file was upstream's verbatim. On 2026-09-17 everything DLSS-IV never calls was
// removed: the linear-buffer copy path (FeedVkImportBuffer, the two buffer copies, the
// buffer ownership transfer, FeedVkHasBufferCopies and the six entry points resolved
// only for them), the single-image forms of the barrier and the ownership transfer, and
// the three handle-cast templates. None of it was in the binary: the linker had already
// dropped it. What remains is unchanged in behaviour. The MIT notice above stays exactly
// as it is: MIT asks for the notice to be retained, not for the code to be left untouched.

// feed_vk.h: raw-Vulkan interop, 32-bit client only. Imports the helper's D3D12 fences and textures as VkSemaphore / VkImage.
// Submits go on DXVK's queue under LockSubmissionQueue, so they cannot race the game's. The mod's images stay GENERAL.

#pragma once
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>    // needs /Iexternal\vulkan (so vk_video/* resolves)
#include <vulkan/vulkan_win32.h>
#include <dxgiformat.h>            // FeedVkFormat's argument
#include <cstdint>

struct FeedVk
{
    HMODULE lib;
    VkDevice dev;

    PFN_vkGetDeviceProcAddr           GetDeviceProcAddr;
    PFN_vkCreateSemaphore             CreateSemaphore;
    PFN_vkDestroySemaphore            DestroySemaphore;
    PFN_vkImportSemaphoreWin32HandleKHR ImportSemaphoreWin32HandleKHR;
    PFN_vkCreateImage                 CreateImage;
    PFN_vkDestroyImage                DestroyImage;
    PFN_vkGetImageMemoryRequirements  GetImageMemoryRequirements;
    PFN_vkAllocateMemory              AllocateMemory;
    PFN_vkFreeMemory                  FreeMemory;
    PFN_vkBindImageMemory             BindImageMemory;
    PFN_vkCmdPipelineBarrier          CmdPipelineBarrier;
    PFN_vkCmdCopyImage                CmdCopyImage;
    PFN_vkCmdBlitImage                CmdBlitImage;
        // Timeline queries, so a wait on a dead host can be bounded. Core in 1.2, else the KHR alias.
    PFN_vkWaitSemaphores              WaitSemaphores;
    PFN_vkGetSemaphoreCounterValue    GetSemaphoreCounterValue;
        // Optional: narrows an imported resource's memory types (FeedVkImportImage).
    PFN_vkGetMemoryWin32HandlePropertiesKHR GetMemoryWin32HandlePropertiesKHR;
        // Optional (Linux sync path): without it the CPU bridge carries the frames.
    PFN_vkGetSemaphoreWin32HandleKHR  GetSemaphoreWin32HandleKHR;

    bool ok;
};

// Resolve from the game's VkDevice; false if an entry is missing (named in `missing`). wineFirst: try DXVK's Wine loader first.
// Vice City NextGen (Claude 2026-11-01): under b-bridge the D3D9 device is the bridge client's and DXVK runs in its x64 server.
// The client exports VCRemoteVk_* entry points that carry the interop and this Vulkan subset to the server; when a loaded module
// exports VCRemoteVk_IsRemote, it is the Vulkan module. FeedVkProc maps the two loader names onto its exports.
#include <psapi.h>
static HMODULE FeedVkRemoteModule()
{
    static HMODULE found = nullptr; static bool looked = false;
    if (looked) return found;
    HMODULE mods[512]; DWORD need = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
        for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; i++)
            if (GetProcAddress(mods[i], "VCRemoteVk_IsRemote")) { found = mods[i]; break; }
    looked = true;   // decided once the device exists (the interop query): the bridge is loaded by then
    return found;
}
static FARPROC FeedVkProc(HMODULE lib, const char *name)
{
    if (lib && lib == FeedVkRemoteModule())
    {
        if (!strcmp(name, "vkGetInstanceProcAddr")) return GetProcAddress(lib, "VCRemoteVk_GetInstanceProcAddr");
        if (!strcmp(name, "vkGetDeviceProcAddr"))   return GetProcAddress(lib, "VCRemoteVk_GetDeviceProcAddr");
    }
    return lib ? GetProcAddress(lib, name) : nullptr;
}

static bool FeedVkLoad(FeedVk *vk, VkDevice device, const char **missing = nullptr, bool wineFirst = false)
{
    const char *unused = nullptr;
    const char *&miss = missing ? *missing : unused;
    miss = nullptr;
    *vk = {};
    vk->dev = device;
        // Under Wine DXVK's device is winevulkan.dll's, and a native vulkan-1.dll would misread it and fault, so winevulkan goes first;
    // on Windows vulkan-1.dll. A module without vkGetDeviceProcAddr is released and the other tried.
    const wchar_t *const order[2] = { wineFirst ? L"winevulkan.dll" : L"vulkan-1.dll", wineFirst ? L"vulkan-1.dll" : L"winevulkan.dll" };
    bool loadedOne = false;
    if (HMODULE remote = FeedVkRemoteModule())
    {
        vk->GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(FeedVkProc(remote, "vkGetDeviceProcAddr"));
        if (vk->GetDeviceProcAddr) { vk->lib = remote; loadedOne = true; }
    }
    for (int i = 0; i < 2 && vk->GetDeviceProcAddr == nullptr; i++)
    {
        HMODULE lib = LoadLibraryW(order[i]);
        if (lib == nullptr) continue;
        loadedOne = true;
        vk->GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(lib, "vkGetDeviceProcAddr"));
        if (vk->GetDeviceProcAddr == nullptr) { FreeLibrary(lib); continue; }
        vk->lib = lib;
    }
    if (vk->GetDeviceProcAddr == nullptr)
    {
        miss = loadedOne ? "vkGetDeviceProcAddr" : "no Vulkan loader: neither vulkan-1.dll nor winevulkan.dll would load";
        return false;
    }

    #define FEED_VK_GET(member, name) \
        vk->member = reinterpret_cast<PFN_vk##member>(vk->GetDeviceProcAddr(device, name)); \
        if (vk->member == nullptr) { miss = name; return false; }
    FEED_VK_GET(CreateSemaphore,             "vkCreateSemaphore")
    FEED_VK_GET(DestroySemaphore,            "vkDestroySemaphore")
    FEED_VK_GET(ImportSemaphoreWin32HandleKHR, "vkImportSemaphoreWin32HandleKHR")
    FEED_VK_GET(CreateImage,                 "vkCreateImage")
    FEED_VK_GET(DestroyImage,                "vkDestroyImage")
    FEED_VK_GET(GetImageMemoryRequirements,  "vkGetImageMemoryRequirements")
    FEED_VK_GET(AllocateMemory,              "vkAllocateMemory")
    FEED_VK_GET(FreeMemory,                  "vkFreeMemory")
    FEED_VK_GET(BindImageMemory,             "vkBindImageMemory")
    FEED_VK_GET(CmdPipelineBarrier,          "vkCmdPipelineBarrier")
    FEED_VK_GET(CmdCopyImage,                "vkCmdCopyImage")
    FEED_VK_GET(CmdBlitImage,                "vkCmdBlitImage")
    #undef FEED_VK_GET


        // Optional: callers check FeedVkHasTimelineQueries first.
    vk->WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(vk->GetDeviceProcAddr(device, "vkWaitSemaphores"));
    if (vk->WaitSemaphores == nullptr)
        vk->WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(vk->GetDeviceProcAddr(device, "vkWaitSemaphoresKHR"));
    vk->GetSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
        vk->GetDeviceProcAddr(device, "vkGetSemaphoreCounterValue"));
    if (vk->GetSemaphoreCounterValue == nullptr)
        vk->GetSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
            vk->GetDeviceProcAddr(device, "vkGetSemaphoreCounterValueKHR"));
    vk->GetMemoryWin32HandlePropertiesKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
        vk->GetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR"));
    vk->GetSemaphoreWin32HandleKHR = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
        vk->GetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));

    vk->ok = true;
    return true;
}

static bool FeedVkHasTimelineQueries(const FeedVk *vk)
{
    return vk->ok && vk->WaitSemaphores != nullptr && vk->GetSemaphoreCounterValue != nullptr;
}

// Wait for `value` or the deadline. VK_SUCCESS, VK_TIMEOUT, or a real error (a lost device), so the relay can tell slow from
// dead; VK_ERROR_UNKNOWN without timeline queries.
static VkResult FeedVkWaitTimelineR(FeedVk *vk, VkSemaphore sem, uint64_t value, uint32_t timeout_ms)
{
    if (!FeedVkHasTimelineQueries(vk) || sem == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;
    VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    wi.semaphoreCount = 1;
    wi.pSemaphores    = &sem;
    wi.pValues        = &value;
    return vk->WaitSemaphores(vk->dev, &wi, static_cast<uint64_t>(timeout_ms) * 1000000ull);
}
static bool FeedVkWaitTimeline(FeedVk *vk, VkSemaphore sem, uint64_t value, uint32_t timeout_ms)
{
    return FeedVkWaitTimelineR(vk, sem, value, timeout_ms) == VK_SUCCESS;
}
// Returns when either timeline reaches its value; the caller reads which.
static VkResult FeedVkWaitAny2R(FeedVk *vk, VkSemaphore a, uint64_t va, VkSemaphore b, uint64_t vb, uint32_t timeout_ms)
{
    if (!FeedVkHasTimelineQueries(vk) || a == VK_NULL_HANDLE || b == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;
    VkSemaphore sems[2] = { a, b }; uint64_t vals[2] = { va, vb };
    VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    wi.flags = VK_SEMAPHORE_WAIT_ANY_BIT; wi.semaphoreCount = 2; wi.pSemaphores = sems; wi.pValues = vals;
    return vk->WaitSemaphores(vk->dev, &wi, static_cast<uint64_t>(timeout_ms) * 1000000ull);
}

static uint64_t FeedVkTimelineValue(FeedVk *vk, VkSemaphore sem)
{
    uint64_t v = 0;
    if (!FeedVkHasTimelineQueries(vk) || sem == VK_NULL_HANDLE) return 0;
    if (vk->GetSemaphoreCounterValue(vk->dev, sem, &v) != VK_SUCCESS) return 0;
    return v;
}

// A D3D12 shared fence imported as a timeline semaphore: the same object.
static VkSemaphore FeedVkImportFence(FeedVk *vk, HANDLE d3d12_fence_handle)
{
    VkSemaphoreTypeCreateInfo tci = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    tci.initialValue  = 0;
    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    sci.pNext = &tci;
    VkSemaphore sem = VK_NULL_HANDLE;
    if (vk->CreateSemaphore(vk->dev, &sci, nullptr, &sem) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkImportSemaphoreWin32HandleInfoKHR imp = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
    imp.semaphore  = sem;
    imp.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    imp.handle     = d3d12_fence_handle;   // not consumed: the driver duplicates it
    if (vk->ImportSemaphoreWin32HandleKHR(vk->dev, &imp) != VK_SUCCESS)
    {
        vk->DestroySemaphore(vk->dev, sem, nullptr);
        return VK_NULL_HANDLE;
    }
    return sem;
}

// A timeline semaphore of the mod's own, exported as OPAQUE_WIN32 if asked (the Linux sync path: Wine intercepts the
// D3D12-typed handle). 1 with a handle, 0 without (the CPU bridge carries the frames), -1 on failure.
static int FeedVkCreateTimeline(FeedVk *vk, bool exportWin32, VkSemaphore *out_sem, HANDLE *out_handle)
{
    *out_sem = VK_NULL_HANDLE; *out_handle = nullptr;
    VkExportSemaphoreCreateInfo exp = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
    exp.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreTypeCreateInfo tci = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    tci.initialValue  = 0;
    const bool tryExport = exportWin32 && vk->GetSemaphoreWin32HandleKHR != nullptr;
    if (tryExport) tci.pNext = &exp;
    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    sci.pNext = &tci;
    VkSemaphore sem = VK_NULL_HANDLE;
    if (vk->CreateSemaphore(vk->dev, &sci, nullptr, &sem) != VK_SUCCESS)
    {
        // A device that refuses the export flag may still give a plain one.
        if (!tryExport) return -1;
        tci.pNext = nullptr;
        if (vk->CreateSemaphore(vk->dev, &sci, nullptr, &sem) != VK_SUCCESS) return -1;
        *out_sem = sem;
        return 0;
    }
    *out_sem = sem;
    if (!tryExport) return 0;
    VkSemaphoreGetWin32HandleInfoKHR gi = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
    gi.semaphore  = sem;
    gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE hnd = nullptr;
    if (vk->GetSemaphoreWin32HandleKHR(vk->dev, &gi, &hnd) != VK_SUCCESS || !hnd) return 0;
    *out_handle = hnd;
    return 1;
}

// How the last import chose its memory type: 1 from the handle's own properties and the
// image's together, 0 the device has no vkGetMemoryWin32HandlePropertiesKHR, 2 the two
// sets share no type, 3 the query failed (0, 2 and 3 take the image's lowest bit).
static int      feedVkImportTypeHow   = 0;
static uint32_t feedVkImportTypeIndex = 0;
// Where an import failed (1 vkCreateImage, 2 vkAllocateMemory, 3 vkBindImageMemory), its VkResult and the types tried.
static int      feedVkImportFailStep   = 0;
static VkResult feedVkImportFailResult = VK_SUCCESS;
static uint32_t feedVkImportTriedTypes = 0;

// A D3D12 shared texture imported as a VkImage (dedicated allocation required). Under Wine it is OPAQUE_WIN32 (Proton refuses
// the D3D12 type) and not narrowed: Wine's handle-properties query returns handle-type constants, not memory-type bits.
static bool FeedVkImportImage(FeedVk *vk, HANDLE d3d12_res_handle, UINT w, UINT h,
                              VkFormat fmt, bool storage, VkExternalMemoryHandleTypeFlagBits handleType,
                              bool narrowByHandle, VkImage *out_image, VkDeviceMemory *out_mem)
{
    *out_image = VK_NULL_HANDLE;
    *out_mem   = VK_NULL_HANDLE;

    VkExternalMemoryImageCreateInfo ext = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    ext.handleTypes = handleType;

    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.pNext         = &ext;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        (storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT);
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    feedVkImportFailStep = 0; feedVkImportFailResult = VK_SUCCESS; feedVkImportTriedTypes = 0;
    feedVkImportTypeHow = 0; feedVkImportTypeIndex = 0;   // this call's, not the last one's
    const VkResult cr = vk->CreateImage(vk->dev, &ici, nullptr, out_image);
    if (cr != VK_SUCCESS) { feedVkImportFailStep = 1; feedVkImportFailResult = cr; *out_image = VK_NULL_HANDLE; return false; }

    VkMemoryRequirements req = {};
    vk->GetImageMemoryRequirements(vk->dev, *out_image, &req);
        // The lowest type allowed by both the handle (required by the spec for a non-opaque handle) and the image; without a usable
    // query, the image's lowest.
    uint32_t allowed = req.memoryTypeBits;
    feedVkImportTypeHow = 0;
    if (narrowByHandle && vk->GetMemoryWin32HandlePropertiesKHR)
    {
        VkMemoryWin32HandlePropertiesKHR hp = { VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR };
        if (vk->GetMemoryWin32HandlePropertiesKHR(vk->dev, handleType,
                                                  d3d12_res_handle, &hp) != VK_SUCCESS) feedVkImportTypeHow = 3;
        else if ((hp.memoryTypeBits & req.memoryTypeBits) == 0)                          feedVkImportTypeHow = 2;
        else { allowed = hp.memoryTypeBits & req.memoryTypeBits;                         feedVkImportTypeHow = 1; }
    }
        // An import binds the host's existing DEFAULT-heap memory, so device-locality needs no check.
    uint32_t type_index = 0;
    for (uint32_t i = 0; i < 32; ++i)
        if (allowed & (1u << i)) { type_index = i; break; }
    feedVkImportTypeIndex = type_index;

    VkMemoryDedicatedAllocateInfo ded = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    ded.image = *out_image;
    VkImportMemoryWin32HandleInfoKHR imp = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
    imp.pNext      = &ded;
    imp.handleType = handleType;
    imp.handle     = d3d12_res_handle;     // duplicated by the driver, not consumed
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.pNext           = &imp;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = type_index;
        // A driver that refuses the first allowed type is given each other before the import fails.
    VkResult ar = vk->AllocateMemory(vk->dev, &mai, nullptr, out_mem);
    feedVkImportTriedTypes = 1u << type_index;
    for (uint32_t i = 0; ar != VK_SUCCESS && i < 32; ++i)
    {
        if (i == type_index || !(allowed & (1u << i))) continue;
        mai.memoryTypeIndex = i; feedVkImportTriedTypes |= 1u << i; *out_mem = VK_NULL_HANDLE;
        ar = vk->AllocateMemory(vk->dev, &mai, nullptr, out_mem);
        if (ar == VK_SUCCESS) feedVkImportTypeIndex = i;
    }
    if (ar != VK_SUCCESS)
    {
        feedVkImportFailStep = 2; feedVkImportFailResult = ar;
        vk->DestroyImage(vk->dev, *out_image, nullptr);
        *out_image = VK_NULL_HANDLE;
        *out_mem   = VK_NULL_HANDLE;
        return false;
    }
    const VkResult br = vk->BindImageMemory(vk->dev, *out_image, *out_mem, 0);
    if (br != VK_SUCCESS)
    {
        feedVkImportFailStep = 3; feedVkImportFailResult = br;
        vk->FreeMemory(vk->dev, *out_mem, nullptr);
        vk->DestroyImage(vk->dev, *out_image, nullptr);
        *out_image = VK_NULL_HANDLE;
        *out_mem   = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Ownership moves between the game's queue family and VK_QUEUE_FAMILY_EXTERNAL around each use from D3D12: the release and
// acquire make writes visible across the API boundary. Layout stays GENERAL. Grouped in one barrier per set.
#define FEED_VK_MAX_GROUP 8

static void FeedVkBarrierN(FeedVk *vk, VkCommandBuffer cb, const VkImage *imgs, uint32_t count,
                           VkImageLayout from, VkImageLayout to)
{
    if (count == 0) return;
    if (count > FEED_VK_MAX_GROUP) count = FEED_VK_MAX_GROUP;
    VkImageMemoryBarrier b[FEED_VK_MAX_GROUP] = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        b[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[i].srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b[i].dstAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b[i].oldLayout           = from;
        b[i].newLayout           = to;
        b[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].image               = imgs[i];
        b[i].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    vk->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           0, 0, nullptr, 0, nullptr, count, b);
}

static void FeedVkExternalTransferN(FeedVk *vk, VkCommandBuffer cb, const VkImage *imgs, uint32_t count,
                                    uint32_t family, bool release)
{
    if (count == 0) return;
    if (count > FEED_VK_MAX_GROUP) count = FEED_VK_MAX_GROUP;
    VkImageMemoryBarrier b[FEED_VK_MAX_GROUP] = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        b[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // Per spec the dst side of a release and the src side of an acquire are ignored.
        b[i].srcAccessMask       = release ? (VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT) : 0;
        b[i].dstAccessMask       = release ? 0 : (VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT);
        b[i].oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b[i].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b[i].srcQueueFamilyIndex = release ? family : VK_QUEUE_FAMILY_EXTERNAL;
        b[i].dstQueueFamilyIndex = release ? VK_QUEUE_FAMILY_EXTERNAL : family;
        b[i].image               = imgs[i];
        b[i].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    vk->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           0, 0, nullptr, 0, nullptr, count, b);
}

static void FeedVkCopyImage(FeedVk *vk, VkCommandBuffer cb, VkImage src, VkImageLayout src_layout,
                            VkImage dst, VkImageLayout dst_layout, UINT w, UINT h)
{
    VkImageCopy c = {};
    c.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    c.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    c.extent         = { w, h, 1 };
    vk->CmdCopyImage(cb, src, src_layout, dst, dst_layout, 1, &c);
}

// Blit for a format or channel-order difference a raw copy cannot handle.
static void FeedVkBlitImage(FeedVk *vk, VkCommandBuffer cb, VkImage src, VkImageLayout src_layout,
                            VkImage dst, VkImageLayout dst_layout, UINT w, UINT h)
{
    VkImageBlit bl = {};
    bl.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    bl.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    bl.srcOffsets[1]  = { static_cast<int32_t>(w), static_cast<int32_t>(h), 1 };
    bl.dstOffsets[1]  = { static_cast<int32_t>(w), static_cast<int32_t>(h), 1 };
    vk->CmdBlitImage(cb, src, src_layout, dst, dst_layout, 1, &bl, VK_FILTER_NEAREST);
}

// DXGI_FORMAT -> VkFormat for the shared-resource formats this project uses.
static VkFormat FeedVkFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:        return VK_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:        return VK_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_UNORM:     return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:    return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT:       return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case DXGI_FORMAT_R32_FLOAT:             return VK_FORMAT_R32_SFLOAT;
    case DXGI_FORMAT_R16G16_FLOAT:          return VK_FORMAT_R16G16_SFLOAT;
    case DXGI_FORMAT_R8_UNORM:              return VK_FORMAT_R8_UNORM;
    default:                                return VK_FORMAT_UNDEFINED;
    }
}
