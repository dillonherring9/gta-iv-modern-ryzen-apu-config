// Phase 3 isolated test (32-bit, like the game): VCRender.asi -> host -> DXVK x64.
//  A. DXVK interop and the Vulkan subset DLSS-IV's game side uses, carried to the host: handles, properties, image info,
//     command pool/buffers, a timeline semaphore, a recorded copy (colour texture -> colour texture, checked by readback)
//     and the depth-stencil -> INTZ copy DLSS-IV's depth snapshot makes, waits on the side channel.
//  B. The bulk upscale (VCB_UpscaleFrame -> native service -> dlssiv-helper-bridge.exe -> FSR 3.1): three frames, each a
//     new scene colour that must reach the output; a foreign pointer refused; status; shutdown, Reset, three more frames.
//     Adapted from research/codex-bbridge-standalone-2026-11-11/tests/rpc-host.cpp.
//  C. No Vulkan loader or graphics driver in this process afterwards.
// Layout: phase3_test.exe, VCRender.asi, VCRender\ (host, DXVK x64 d3d9/dxgi, dxvk.conf, dlssiv-helper-bridge.exe),
// DLSSIV\ (FidelityFX DLLs). usage: phase3_test.exe [results.txt]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include "../src/upscale/dxvk_interop_min.h"
#include "../src/upscale/vc_upscale_wire.h"
#include "../src/shared/vcr_maps.h"

static FILE* out;
static int failures;
static bool Check(bool ok, const char* what, long detail = 0) {
  std::fprintf(out, "RESULT %s %s (%ld)\n", ok ? "PASS" : "FAIL", what, detail); std::fflush(out);
  if (!ok) ++failures; return ok;
}
static float Half(uint16_t h) {
  const int sign = h & 0x8000 ? -1 : 1, exp = (h >> 10) & 31;
  const float mant = (float) (h & 1023);
  if (exp == 31) return mant ? NAN : sign * INFINITY;
  return exp ? sign * std::ldexp(1.0f + mant / 1024.0f, exp - 15) : sign * std::ldexp(mant, -24);
}
static bool Fill(IDirect3DDevice9* d, IDirect3DTexture9* t, D3DCOLOR color) {
  IDirect3DSurface9 *old = nullptr, *target = nullptr;
  d->GetRenderTarget(0, &old); t->GetSurfaceLevel(0, &target);
  const bool ok = target && SUCCEEDED(d->SetRenderTarget(0, target)) && SUCCEEDED(d->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0));
  if (old) { d->SetRenderTarget(0, old); old->Release(); }
  if (target) target->Release(); return ok;
}
static bool Texture(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT f, IDirect3DTexture9** t, DWORD usage = D3DUSAGE_RENDERTARGET) {
  return SUCCEEDED(d->CreateTexture(w, h, 1, usage, f, D3DPOOL_DEFAULT, t, nullptr)) && *t;
}
static bool Read(IDirect3DDevice9* d, IDirect3DTexture9* t, UINT w, UINT h, float (&px)[4]) {
  IDirect3DSurface9 *gpu = nullptr, *cpu = nullptr;
  bool ok = SUCCEEDED(t->GetSurfaceLevel(0, &gpu)) && gpu &&
    SUCCEEDED(d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &cpu, nullptr)) && cpu &&
    SUCCEEDED(d->GetRenderTargetData(gpu, cpu));
  D3DLOCKED_RECT lock = {};
  if (ok && (ok = SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)))) {
    const uint16_t* p = (const uint16_t*) ((const unsigned char*) lock.pBits + (h / 2) * lock.Pitch) + (w / 2) * 4;
    for (int i = 0; i < 4; ++i) px[i] = Half(p[i]);
    cpu->UnlockRect();
  }
  if (cpu) cpu->Release(); if (gpu) gpu->Release(); return ok;
}
static bool Utf8(const wchar_t* w, char* o, int cap) { return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, o, cap, nullptr, nullptr) > 0; }
using UpscaleFn = HRESULT (WINAPI*)(const VCBConfig*, const VCBInput*, VCBResult*);
using DeviceFn = HRESULT (WINAPI*)(void*, VCBResult*);
struct Set { IDirect3DTexture9 *scene = nullptr, *home = nullptr, *depth = nullptr, *mv = nullptr; };
static void Free(Set& s) { IDirect3DTexture9** all[4] = { &s.scene, &s.home, &s.depth, &s.mv }; for (auto t : all) if (*t) { (*t)->Release(); *t = nullptr; } }

static void Frames(IDirect3DDevice9* d, UpscaleFn up, const VCBConfig& c, Set& s, uint64_t& seq, UINT rw, UINT rh, UINT ow, UINT oh, const char* label) {
  char what[128];
  Fill(d, s.home, D3DCOLOR_ARGB(255, 128, 128, 128)); Fill(d, s.depth, D3DCOLOR_ARGB(255, 128, 128, 128)); Fill(d, s.mv, 0);
  const D3DCOLOR colors[3] = { D3DCOLOR_ARGB(255, 192, 24, 8), D3DCOLOR_ARGB(255, 8, 192, 24), D3DCOLOR_ARGB(255, 24, 8, 192) };
  for (unsigned n = 0; n < 3; ++n) {
    Fill(d, s.scene, colors[n]);
    VCBInput in = {}; in.size = sizeof(in); in.version = VCB_ABI_VERSION;
    in.device = d; in.scene = s.scene; in.home = s.home; in.depth = s.depth; in.mv = s.mv;
    in.frame.sequence = ++seq; in.frame.render_width = rw; in.frame.render_height = rh; in.frame.output_width = ow; in.frame.output_height = oh;
    in.frame.flags = VCB_FRAME_VALID | VCB_FRAME_GUIDES_VALID | (n == 0 ? VCB_FRAME_RESET : VCB_FRAME_PREVIOUS_VALID);
    in.frame.camera_near = 0.1f; in.frame.fov_y_rad = 1.0f; in.frame.frame_time_ms = 16.667f; in.frame.mv_scale_x = 1.0f; in.frame.mv_scale_y = 1.0f;
    VCBResult r = {}; r.size = sizeof(r); r.version = VCB_ABI_VERSION;
    const HRESULT hr = up(&c, &in, &r);
    r.message[sizeof(r.message) - 1] = 0;
    std::fprintf(out, "%s frame %u: hr=%08lx service=%08lx backend=%d flags=%u seq=%llu %s\n", label, n + 1,
                 (unsigned long) hr, (unsigned long) r.hr, r.backend, r.flags, (unsigned long long) r.sequence, r.message);
    std::snprintf(what, sizeof what, "%s frame %u upscaled by FSR in the host and copied home", label, n + 1);
    Check(SUCCEEDED(hr) && SUCCEEDED((HRESULT) r.hr) && r.sequence == seq && (r.flags & 3u) == 3u && r.backend == 1, what, hr);
    float px[4] = {};
    const bool read = Read(d, s.home, ow, oh, px);
    std::fprintf(out, "%s frame %u readback %.5f %.5f %.5f %.5f\n", label, n + 1, px[0], px[1], px[2], px[3]);
    bool finite = read; for (float v : px) finite = finite && std::isfinite(v);
    std::snprintf(what, sizeof what, "%s frame %u output shows this frame's colour", label, n + 1);
    Check(finite && px[n] > 0.1f && px[n] > px[(n + 1) % 3] * 2.0f && px[n] > px[(n + 2) % 3] * 2.0f, what);
  }
}

// Part A: the remote interop
static void Interop(HMODULE vcr, IDirect3DDevice9* d) {
  ID3D9VkInteropDevice* idev = nullptr;
  HRESULT hr = d->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**) &idev);
  if (!Check(SUCCEEDED(hr) && idev, "device answers ID3D9VkInteropDevice", hr)) return;
  auto isRemote = (int (WINAPI*)()) GetProcAddress(vcr, "VCRemoteVk_IsRemote");
  auto gipa = (PFN_vkGetInstanceProcAddr) GetProcAddress(vcr, "VCRemoteVk_GetInstanceProcAddr");
  auto gdpaX = (PFN_vkGetDeviceProcAddr) GetProcAddress(vcr, "VCRemoteVk_GetDeviceProcAddr");
  Check(isRemote && isRemote() == 2 && gipa && gdpaX, "VCRemoteVk exports (wire 2)", isRemote ? isRemote() : -1);
  VkInstance inst = nullptr; VkPhysicalDevice phys = nullptr; VkDevice dev = nullptr; VkQueue q = nullptr; uint32_t qi = 0, qf = 0;
  idev->GetVulkanHandles(&inst, &phys, &dev); idev->GetSubmissionQueue(&q, &qi, &qf);
  Check(inst && phys && dev && q, "Vulkan session bound (handles and queue)", (long) qf);
  auto props2 = (PFN_vkGetPhysicalDeviceProperties2) gipa(inst, "vkGetPhysicalDeviceProperties2");
  VkPhysicalDeviceIDProperties idp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
  VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idp };
  if (props2) props2(phys, &p2);
  std::fprintf(out, "INFO host GPU \"%s\" vendor %04X LUID valid %u\n", p2.properties.deviceName, p2.properties.vendorID, idp.deviceLUIDValid);
  Check(props2 && p2.properties.vendorID != 0 && p2.properties.deviceName[0], "physical device properties from the host", (long) p2.properties.vendorID);
  auto gdpa = (PFN_vkGetDeviceProcAddr) gipa(inst, "vkGetDeviceProcAddr");
#define G(n) auto n = (PFN_vk##n) gdpa(dev, "vk" #n)
  G(CreateCommandPool); G(AllocateCommandBuffers); G(BeginCommandBuffer); G(EndCommandBuffer); G(ResetCommandBuffer);
  G(CmdPipelineBarrier); G(CmdCopyImage); G(QueueSubmit); G(CreateSemaphore); G(WaitSemaphores); G(GetSemaphoreCounterValue);
  G(CreateImage); G(AllocateMemory); G(ImportSemaphoreWin32HandleKHR);
#undef G
  Check(CreateCommandPool && AllocateCommandBuffers && BeginCommandBuffer && EndCommandBuffer && ResetCommandBuffer && CmdPipelineBarrier &&
        CmdCopyImage && QueueSubmit && CreateSemaphore && WaitSemaphores && GetSemaphoreCounterValue && CreateImage && AllocateMemory &&
        ImportSemaphoreWin32HandleKHR, "every entry point FeedVkLoad and the ring need resolves");
  VkCommandPool pool = 0; VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO }; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = qf;
  VkResult vr = CreateCommandPool(dev, &pci, nullptr, &pool);
  VkCommandBuffer cb[2] = {}; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 2;
  if (vr == VK_SUCCESS) vr = AllocateCommandBuffers(dev, &ai, cb);
  VkSemaphoreTypeCreateInfo tci = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO }; tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &tci }; VkSemaphore sem = 0;
  if (vr == VK_SUCCESS) vr = CreateSemaphore(dev, &sci, nullptr, &sem);
  if (!Check(vr == VK_SUCCESS && pool && cb[0] && cb[1] && sem, "command pool, two command buffers, timeline semaphore", vr)) { idev->Release(); return; }

  // colour copy: A (filled green) -> B (filled red); B must read green afterwards
  IDirect3DTexture9 *a = nullptr, *b = nullptr;
  Texture(d, 64, 64, D3DFMT_A16B16G16R16F, &a); Texture(d, 64, 64, D3DFMT_A16B16G16R16F, &b);
  Fill(d, a, D3DCOLOR_ARGB(255, 0, 200, 0)); Fill(d, b, D3DCOLOR_ARGB(255, 200, 0, 0));
  ID3D9VkInteropTexture *ia = nullptr, *ib = nullptr;
  a->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**) &ia); b->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**) &ib);
  VkImage imgA = 0, imgB = 0; VkImageLayout la = VK_IMAGE_LAYOUT_UNDEFINED, lb = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateInfo ca = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO }, cbi = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
  HRESULT ha = ia ? ia->GetVulkanImageInfo(&imgA, &la, &ca) : E_FAIL, hb = ib ? ib->GetVulkanImageInfo(&imgB, &lb, &cbi) : E_FAIL;
  std::fprintf(out, "INFO texture A image 0x%llx %ux%u fmt %d layout %d usage 0x%x\n", (unsigned long long) imgA, ca.extent.width, ca.extent.height, (int) ca.format, (int) la, (unsigned) ca.usage);
  Check(SUCCEEDED(ha) && SUCCEEDED(hb) && imgA && imgB && ca.extent.width == 64 && ca.format == VK_FORMAT_R16G16B16A16_SFLOAT, "texture image info from the host", ha);
  // as DLSS-IV: settle pending clears (same-layout transition), flush, then copy on our own command buffer
  VkImageSubresourceRange rng = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  if (ia) idev->TransitionTextureLayout(ia, &rng, la, la);
  if (ib) idev->TransitionTextureLayout(ib, &rng, lb, lb);
  idev->FlushRenderingCommands();
  VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  BeginCommandBuffer(cb[0], &bi);
  VkImageMemoryBarrier bar[2] = { { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER }, { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER } };
  bar[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; bar[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; bar[0].oldLayout = la; bar[0].newLayout = la;
  bar[0].srcQueueFamilyIndex = bar[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; bar[0].image = imgA; bar[0].subresourceRange = rng;
  bar[1] = bar[0]; bar[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; bar[1].oldLayout = lb; bar[1].newLayout = lb; bar[1].image = imgB;
  CmdPipelineBarrier(cb[0], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, bar);
  VkImageCopy reg = {}; reg.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; reg.dstSubresource = reg.srcSubresource; reg.extent = { 64, 64, 1 };
  CmdCopyImage(cb[0], imgA, la, imgB, lb, 1, &reg);
  bar[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; bar[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  bar[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; bar[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  CmdPipelineBarrier(cb[0], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, bar);
  EndCommandBuffer(cb[0]);
  uint64_t one = 1;
  VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO }; tl.signalSemaphoreValueCount = 1; tl.pSignalSemaphoreValues = &one;
  VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &tl }; si.commandBufferCount = 1; si.pCommandBuffers = &cb[0]; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &sem;
  idev->LockSubmissionQueue(); vr = QueueSubmit(q, 1, &si, VK_NULL_HANDLE); idev->ReleaseSubmissionQueue();
  VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO }; wi.semaphoreCount = 1; wi.pSemaphores = &sem; wi.pValues = &one;
  VkResult wr = WaitSemaphores(dev, &wi, 2000000000ull);
  uint64_t counter = 0; GetSemaphoreCounterValue(dev, sem, &counter);
  Check(vr == VK_SUCCESS && wr == VK_SUCCESS && counter >= 1, "recorded copy submitted; timeline reached on the side channel", wr);
  float px[4] = {}; Read(d, b, 64, 64, px);
  std::fprintf(out, "INFO texture B after the Vulkan copy: %.3f %.3f %.3f %.3f\n", px[0], px[1], px[2], px[3]);
  Check(px[1] > 0.5f && px[0] < 0.1f, "texture B now holds texture A's colour (copy ran on the real device, in order)");

  // DLSS-IV's depth snapshot: the scene depth-stencil surface copied into an INTZ texture (depth|stencil aspects)
  IDirect3DSurface9* ds = nullptr; IDirect3DTexture9* intz = nullptr;
  hr = d->CreateDepthStencilSurface(64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds, nullptr);
  bool haveIntz = SUCCEEDED(d->CreateTexture(64, 64, 1, D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT) MAKEFOURCC('I', 'N', 'T', 'Z'), D3DPOOL_DEFAULT, &intz, nullptr)) && intz;
  Check(SUCCEEDED(hr) && ds && haveIntz, "D24S8 depth surface and INTZ texture", hr);
  if (ds && intz) {
    IDirect3DSurface9* oldDs = nullptr; d->GetDepthStencilSurface(&oldDs);
    d->SetDepthStencilSurface(ds); d->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 0.25f, 7);
    d->SetDepthStencilSurface(oldDs); if (oldDs) oldDs->Release();
    ID3D9VkInteropTexture *ids = nullptr, *iz = nullptr;
    ds->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**) &ids); intz->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**) &iz);
    VkImage dsImg = 0, zImg = 0; VkImageLayout dl = VK_IMAGE_LAYOUT_UNDEFINED, zl = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo dci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO }, zci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    HRESULT h1 = ids ? ids->GetVulkanImageInfo(&dsImg, &dl, &dci) : E_FAIL, h2 = iz ? iz->GetVulkanImageInfo(&zImg, &zl, &zci) : E_FAIL;
    std::fprintf(out, "INFO depth surface image 0x%llx fmt %d layout %d; INTZ image 0x%llx fmt %d layout %d\n",
                 (unsigned long long) dsImg, (int) dci.format, (int) dl, (unsigned long long) zImg, (int) zci.format, (int) zl);
    Check(SUCCEEDED(h1) && SUCCEEDED(h2) && dsImg && zImg && dci.format == zci.format, "standalone depth surface and INTZ texture expose the same Vulkan format", (long) dci.format);
    VkImageSubresourceRange drng = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
    if (ids) idev->TransitionTextureLayout(ids, &drng, dl, dl);
    idev->FlushRenderingCommands();
    BeginCommandBuffer(cb[1], &bi);
    VkImageCopy dreg = {}; dreg.srcSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1 }; dreg.dstSubresource = dreg.srcSubresource; dreg.extent = { 64, 64, 1 };
    VkImageMemoryBarrier db[2] = { { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER }, { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER } };
    db[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; db[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; db[0].oldLayout = dl; db[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    db[0].srcQueueFamilyIndex = db[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; db[0].image = dsImg; db[0].subresourceRange = drng;
    db[1] = db[0]; db[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; db[1].oldLayout = zl; db[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; db[1].image = zImg;
    CmdPipelineBarrier(cb[1], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, db);
    CmdCopyImage(cb[1], dsImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, zImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &dreg);
    db[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; db[0].newLayout = dl; db[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; db[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    db[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; db[1].newLayout = zl; db[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; db[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    CmdPipelineBarrier(cb[1], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, db);
    EndCommandBuffer(cb[1]);
    uint64_t two = 2; tl.pSignalSemaphoreValues = &two; si.pCommandBuffers = &cb[1];
    idev->LockSubmissionQueue(); vr = QueueSubmit(q, 1, &si, VK_NULL_HANDLE); idev->ReleaseSubmissionQueue();
    wi.pValues = &two; wr = WaitSemaphores(dev, &wi, 2000000000ull);
    Check(vr == VK_SUCCESS && wr == VK_SUCCESS, "depth-stencil -> INTZ snapshot copy submitted and retired", wr);
    // sample the INTZ texture onto a render target: depth 0.25 must come through
    IDirect3DTexture9* rt = nullptr; Texture(d, 64, 64, D3DFMT_A16B16G16R16F, &rt);
    IDirect3DSurface9* rts = nullptr; if (rt) rt->GetSurfaceLevel(0, &rts);
    IDirect3DSurface9* oldRt = nullptr; d->GetRenderTarget(0, &oldRt);
    d->SetRenderTarget(0, rts); d->SetTexture(0, intz);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_LIGHTING, FALSE); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    struct V { float x, y, z, w, u, v; } quad[4] = { {-0.5f, -0.5f, 0, 1, 0, 0}, {63.5f, -0.5f, 0, 1, 1, 0}, {-0.5f, 63.5f, 0, 1, 0, 1}, {63.5f, 63.5f, 0, 1, 1, 1} };
    d->BeginScene(); d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V)); d->EndScene();
    d->SetTexture(0, nullptr); d->SetRenderTarget(0, oldRt); if (oldRt) oldRt->Release();
    float zp[4] = {}; if (rt) Read(d, rt, 64, 64, zp);
    std::fprintf(out, "INFO INTZ sampled after the snapshot: %.4f %.4f %.4f %.4f (depth cleared to 0.25)\n", zp[0], zp[1], zp[2], zp[3]);
    Check(std::fabs(zp[0] - 0.25f) < 0.01f, "INTZ snapshot holds the scene depth (0.25)");
    if (rts) rts->Release(); if (rt) rt->Release();
    if (ids) ids->Release(); if (iz) iz->Release();
  }
  if (ds) ds->Release(); if (intz) intz->Release();
  if (ia) ia->Release(); if (ib) ib->Release();
  a->Release(); b->Release();
  idev->Release();
}

int main(int argc, char** argv) {
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  GetModuleFileNameW(nullptr, dir, MAX_PATH); *wcsrchr(dir, L'\\') = 0;
  out = fopen(argc > 1 ? argv[1] : "phase3-results.txt", "w");
  if (!out) return 3;
  swprintf(path, MAX_PATH, L"%ls\\VCRender.asi", dir);
  HMODULE vcr = LoadLibraryW(path);
  auto init = vcr ? (void (__cdecl*)()) GetProcAddress(vcr, "VCR_InitStandalone") : nullptr;
  if (!Check(init != nullptr, "VCRender.asi loaded")) return 1;
  init();
  auto up = (UpscaleFn) GetProcAddress(vcr, "VCB_UpscaleFrame");
  auto status = (DeviceFn) GetProcAddress(vcr, "VCB_UpscaleStatus");
  auto shutdown = (DeviceFn) GetProcAddress(vcr, "VCB_UpscaleShutdown");
  if (!Check(up && status && shutdown, "VCB upscale exports")) return 1;
  auto create = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(vcr, "Direct3DCreate9");
  IDirect3D9* d3d = create(D3D_SDK_VERSION);
  if (!Check(d3d != nullptr, "Direct3DCreate9 through VCRender")) return 1;
  WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRPhase3";
  RegisterClassW(&wc);
  HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"VCRender phase 3 isolated test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 336, 359, nullptr, nullptr, wc.hInstance, nullptr);
  D3DPRESENT_PARAMETERS pp = {}; pp.Windowed = TRUE; pp.hDeviceWindow = wnd;
  pp.BackBufferWidth = 320; pp.BackBufferHeight = 320; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
  IDirect3DDevice9* d = nullptr;
  HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &d);
  if (!Check(SUCCEEDED(hr) && d, "CreateDevice through VCRender", hr)) return 1;

  Interop(vcr, d);

  VCBConfig c = {}; c.size = sizeof(c); c.version = VCB_ABI_VERSION; c.backend = 1; c.fsr_version = 3; c.preset = 11;
  c.timeout_ms = 30000; c.depth_inverted = 1; c.flags_override = -1;
  swprintf(path, MAX_PATH, L"%ls\\VCRender\\dlssiv-helper-bridge.exe", dir); Utf8(path, c.helper_path, sizeof c.helper_path);
  swprintf(path, MAX_PATH, L"%ls\\DLSSIV", dir); Utf8(path, c.vendor_dir, sizeof c.vendor_dir);
  Set s; uint64_t seq = 0;
  Check(Texture(d, 64, 64, D3DFMT_A16B16G16R16F, &s.scene) && Texture(d, 96, 96, D3DFMT_A16B16G16R16F, &s.home) &&
        Texture(d, 64, 64, D3DFMT_R32F, &s.depth) && Texture(d, 64, 64, D3DFMT_G16R16F, &s.mv), "scene, home and guide textures");
  if (s.scene && s.home && s.depth && s.mv) Frames(d, up, c, s, seq, 64, 64, 96, 96, "first");
  VCBInput bad = {}; bad.size = sizeof(bad); bad.version = VCB_ABI_VERSION; bad.device = d; bad.scene = &seq; bad.home = s.home;
  bad.frame.sequence = seq + 1; bad.frame.render_width = bad.frame.render_height = 64; bad.frame.output_width = bad.frame.output_height = 96;
  VCBResult r = {}; r.size = sizeof(r); r.version = VCB_ABI_VERSION;
  Check(FAILED(up(&c, &bad, &r)), "foreign texture pointer refused");
  VCBResult st = {}; st.size = sizeof(st); st.version = VCB_ABI_VERSION;
  hr = status(d, &st);
  Check(SUCCEEDED(hr) && st.sequence == seq, "status reports the last frame", hr);
  Free(s);
  VCBResult sd = {}; sd.size = sizeof(sd); sd.version = VCB_ABI_VERSION;
  hr = shutdown(d, &sd);
  Check(SUCCEEDED(hr) && (sd.flags & VCB_RESULT_SHUTDOWN), "service shutdown before Reset", hr);
  pp.BackBufferWidth = 400; pp.BackBufferHeight = 300;
  hr = d->Reset(&pp);
  Check(SUCCEEDED(hr), "device Reset", hr);
  Check(Texture(d, 80, 64, D3DFMT_A16B16G16R16F, &s.scene) && Texture(d, 120, 96, D3DFMT_A16B16G16R16F, &s.home) &&
        Texture(d, 80, 64, D3DFMT_R32F, &s.depth) && Texture(d, 80, 64, D3DFMT_G16R16F, &s.mv), "textures recreated after Reset");
  if (s.scene && s.home && s.depth && s.mv) Frames(d, up, c, s, seq, 80, 64, 120, 96, "after-reset");
  Free(s);
  // D. Throughput at the game's sizes: 120 frames of 1280x800 -> 1920x1200, each with a Present, like the game's loop.
  {
    Set g;
    Check(Texture(d, 1280, 800, D3DFMT_A16B16G16R16F, &g.scene) && Texture(d, 1920, 1200, D3DFMT_A16B16G16R16F, &g.home) &&
          Texture(d, 1280, 800, D3DFMT_R32F, &g.depth) && Texture(d, 1280, 800, D3DFMT_G16R16F, &g.mv), "game-size textures");
    if (g.scene && g.home && g.depth && g.mv) {
      Fill(d, g.depth, D3DCOLOR_ARGB(255, 128, 128, 128)); Fill(d, g.mv, 0);
      LARGE_INTEGER f0, a, b, e; QueryPerformanceFrequency(&f0);
      double callMs = 0, callMax = 0; int ok = 0;
      QueryPerformanceCounter(&a);
      for (unsigned n = 0; n < 120; ++n) {
        Fill(d, g.scene, D3DCOLOR_ARGB(255, (n * 2) & 255, 64, 255 - ((n * 2) & 255)));
        VCBInput in = {}; in.size = sizeof(in); in.version = VCB_ABI_VERSION;
        in.device = d; in.scene = g.scene; in.home = g.home; in.depth = g.depth; in.mv = g.mv;
        in.frame.sequence = ++seq; in.frame.render_width = 1280; in.frame.render_height = 800; in.frame.output_width = 1920; in.frame.output_height = 1200;
        in.frame.flags = VCB_FRAME_VALID | VCB_FRAME_GUIDES_VALID | (n == 0 ? VCB_FRAME_RESET : VCB_FRAME_PREVIOUS_VALID);
        in.frame.camera_near = 0.1f; in.frame.fov_y_rad = 1.0f; in.frame.frame_time_ms = 16.667f; in.frame.mv_scale_x = 1.0f; in.frame.mv_scale_y = 1.0f;
        VCBResult r = {}; r.size = sizeof(r); r.version = VCB_ABI_VERSION;
        QueryPerformanceCounter(&b);
        const HRESULT hr = up(&c, &in, &r);
        QueryPerformanceCounter(&e);
        const double ms = (double) (e.QuadPart - b.QuadPart) * 1000.0 / (double) f0.QuadPart;
        callMs += ms; if (ms > callMax) callMax = ms;
        if (SUCCEEDED(hr) && SUCCEEDED((HRESULT) r.hr)) ok++;
        d->Present(nullptr, nullptr, nullptr, nullptr);
      }
      QueryPerformanceCounter(&e);
      const double total = (double) (e.QuadPart - a.QuadPart) * 1000.0 / (double) f0.QuadPart;
      float px[4] = {}; Read(d, g.home, 1920, 1200, px);
      const float wantR = (float) ((119 * 2) & 255) / 255.0f, wantB = (float) (255 - ((119 * 2) & 255)) / 255.0f;
      std::fprintf(out, "INFO throughput: 120 frames 1280x800 -> 1920x1200, %.2f ms per upscale call (max %.2f), %.2f ms per frame with Present; last output %.3f %.3f %.3f (want %.3f %.3f %.3f)\n",
                   callMs / 120.0, callMax, total / 120.0, px[0], px[1], px[2], wantR, 64 / 255.0f, wantB);
      Check(ok == 120, "120 game-size frames upscaled", ok);
      Check(std::fabs(px[0] - wantR) < 0.05f && std::fabs(px[2] - wantB) < 0.05f, "the last game-size output is the last frame's colour");
    }
    Free(g);
  }
  for (int i = 0; i < 30; ++i) { d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 64), 1.0f, 0); d->Present(nullptr, nullptr, nullptr, nullptr); }
  bool vk = GetModuleHandleW(L"winevulkan.dll") || GetModuleHandleW(L"vulkan-1.dll");
  VcrMaps m; bool maps = vcrReadMaps(m);
  std::fprintf(out, "INFO Linux maps below 4 GiB: %s mapped %llu MiB, graphics %llu MiB (LLVM %llu, GPU buffers %llu)\n", maps ? "" : "(unavailable)",
               (unsigned long long) (m.mapped >> 20), (unsigned long long) (m.graphics >> 20), (unsigned long long) (m.llvm >> 20), (unsigned long long) (m.gpuBuffers >> 20));
  Check(!vk, "no Vulkan loader in this 32-bit process");
  Check(maps && m.graphics == 0, "no graphics driver or LLVM mapped in this process", (long) (m.graphics >> 20));
  ULONG left = d->Release();
  Check(left == 0, "device released", (long) left);
  left = d3d->Release();
  Check(left == 0, "IDirect3D9 released", (long) left);
  DestroyWindow(wnd);
  std::fprintf(out, "SUMMARY %s (%d failed)\n", failures ? "FAIL" : "PASS", failures);
  std::fclose(out);
  return failures ? 2 : 0;
}
