// Native64 upscale: DXVK x64 -> shared images -> matched DLSS-IV x64 helper.
// Copyright 2026 Vice City project contributors. MIT.
// VCRender (2026-11-13): FAST mode. The two timeline semaphores are created here and opened by the helper as its fences
// (the "Linux sync path" of DLSS-IV's protocol, which this machine ran FAST in-process). Per frame: the copy-in submit
// signals in = n, the helper's queue waits in >= n, runs FSR and signals out = n, and the copy-out submit waits out >= n
// on the GPU. No CPU wait per frame; a ring of command buffers and a watchdog that releases a stuck GPU wait if the helper
// dies or hangs. The CPU-relayed BRIDGE mode stays as the fallback when the probe fails.
// Uses DLSS-IV's Apache-2.0 transport headers and the MIT Feeder protocol;
// retain their original notices when redistributing this source.
#include <windows.h>
#include <d3d9.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <stdlib.h>
#include "vc_upscale_server.h"
#include "feed_vk.h"
#include "feed_ipc.h"
#include "dxvk_interop_min.h"

#ifndef VCB_HELPER_BUILD_ID
#define VCB_HELPER_BUILD_ID "60f29688-vcbridge1"
#endif

namespace {
constexpr unsigned kInputMask = (1u << FEED_COLOR) | (1u << FEED_DEPTH) | (1u << FEED_MV);
constexpr unsigned kOutputMask = 1u << FEED_OUTPUT;

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
};
struct State {
  IDirect3DDevice9* device = nullptr; // held alive by interop's COM reference
  ID3D9VkInteropDevice* interop = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice vkDevice = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = 0;
  FeedVk vk = {};
  PFN_vkGetInstanceProcAddr gipa = nullptr;
  PFN_vkQueueSubmit queueSubmit = nullptr;
  PFN_vkCreateCommandPool createPool = nullptr;
  PFN_vkDestroyCommandPool destroyPool = nullptr;
  PFN_vkAllocateCommandBuffers allocateBuffers = nullptr;
  PFN_vkBeginCommandBuffer begin = nullptr;
  PFN_vkEndCommandBuffer end = nullptr;
  PFN_vkResetCommandBuffer reset = nullptr;
  PFN_vkCmdClearColorImage clear = nullptr;
  PFN_vkGetPhysicalDeviceFormatProperties formatProperties = nullptr;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  VkSemaphore retired = VK_NULL_HANDLE;
  uint64_t submitted = 0, sequence = 0, helperFrame = 0;
  VkImage shared[FEED_SLOTS] = {};
  VkDeviceMemory memory[FEED_SLOTS] = {};
  HANDLE imageHandles[FEED_SLOTS] = {};
  bool layoutInit = false, released[FEED_SLOTS] = {};
  HANDLE helper = nullptr, pipe = nullptr;
  bool poisoned = false;
  ULONGLONG deadline = 0;
  VCBConfig config = {};
  VCBFrame shape = {};
  VkFormat sceneFormat = VK_FORMAT_UNDEFINED, homeFormat = VK_FORMAT_UNDEFINED;
  VkFormat outputFormat = VK_FORMAT_UNDEFINED;
  int backend = -1;
  uint32_t detail = 0;
  // FAST mode (VCRender)
  static constexpr unsigned kRing = 4;
  VkSemaphore semIn = VK_NULL_HANDLE, semOut = VK_NULL_HANDLE;
  PFN_vkSignalSemaphore signal = nullptr;
  bool fast = false;
  VkCommandBuffer ring[kRing][2] = {};
  uint64_t ringValue[kRing] = {};
  volatile uint64_t lastFastN = 0;   // highest frame whose copy-out waits on semOut (read by the watchdog)
  volatile LONG unstuck = 0;
  // timing
  uint64_t statFrames = 0; double statMs = 0, statMax = 0;
};
constexpr uint64_t kProbeN = 4;   // DLSS-IV's kIvtProbeN: frame numbers start above it
std::mutex serviceMutex;
State* active = nullptr;
// A wedged GPU may still read these resources. They remain pinned until process
// exit rather than being freed under GPU work. This is an explicit failure path.
std::vector<State*> quarantined;

void (*gLog)(const char*) = nullptr;
void Trace(const char* message) {
  char line[512];
  _snprintf_s(line, sizeof(line), _TRUNCATE, "[VCB native64] %s\n", message);
  OutputDebugStringA(line);
  if (gLog) gLog(message);
}
// The watchdog: if the helper exits or stops answering while copy-outs wait on its out-semaphore, the GPU would wait
// forever (and DXVK's frame throttle with it). A CPU signal of the out-semaphore to the last frame lets the GPU go on.
SRWLOCK watchLock = SRWLOCK_INIT;
State* watched = nullptr;
bool watchStarted = false;
void Unstick(State& s, const char* why) {
  if (InterlockedExchange(&s.unstuck, 1)) return;
  uint64_t top = s.lastFastN, now = 0;
  if (s.semOut && s.vk.GetSemaphoreCounterValue) s.vk.GetSemaphoreCounterValue(s.vkDevice, s.semOut, &now);
  if (s.signal && s.semOut && top > now) {
    VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO }; si.semaphore = s.semOut; si.value = top;
    s.signal(s.vkDevice, &si);
  }
  s.poisoned = true;
  char m[256]; _snprintf_s(m, sizeof m, _TRUNCATE, "FAST sync released by the watchdog (%s): out-semaphore %llu -> %llu; upscaling stops", why,
                           (unsigned long long) now, (unsigned long long) top);
  Trace(m);
}
DWORD WINAPI Watchdog(void*) {
  uint64_t seen = 0; ULONGLONG since = GetTickCount64(); State* last = nullptr;
  for (;;) {
    Sleep(250);
    AcquireSRWLockShared(&watchLock);
    State* s = watched;
    if (s && s->fast && !s->unstuck) {
      if (s != last) { last = s; seen = 0; since = GetTickCount64(); }
      uint64_t now = 0;
      if (s->vk.GetSemaphoreCounterValue) s->vk.GetSemaphoreCounterValue(s->vkDevice, s->semOut, &now);
      const bool behind = now < s->lastFastN;
      if (now != seen || !behind) { seen = now; since = GetTickCount64(); }
      if (s->helper && WaitForSingleObject(s->helper, 0) == WAIT_OBJECT_0) Unstick(*s, "the helper exited");
      else if (behind && GetTickCount64() - since > 3000) Unstick(*s, "no FSR frame finished for 3 s");
    }
    ReleaseSRWLockShared(&watchLock);
  }
  return 0;
}
HRESULT Fail(VCBResult& r, HRESULT hr, const char* message) {
  const uint64_t sequence = r.sequence;
  VCB_InitResult(r, hr, message); r.sequence = sequence; Trace(message); return hr;
}
bool Wide(const char* s, std::wstring& out) {
  if (!s || !*s) return false;
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
  if (n < 2 || n > 32000) return false;
  std::vector<wchar_t> b(n);
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, b.data(), n)) return false;
  out.assign(b.data());
  return (out.size() > 2 && out[1] == L':' && (out[2] == L'\\' || out[2] == L'/')) ||
         (out.size() > 2 && out[0] == L'\\' && out[1] == L'\\');
}
// Full Windows argv quoting, including trailing slashes and literal quotes.
std::wstring Quote(const std::wstring& s) {
  std::wstring r = L"\""; size_t slashes = 0;
  for (wchar_t c : s) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') r.append(slashes * 2 + 1, L'\\');
    else r.append(slashes, L'\\');
    r += c; slashes = 0;
  }
  r.append(slashes * 2, L'\\'); r += L'\"'; return r;
}
// Both directions are overlapped and bounded. On the exceptional cancellation
// path the request storage is retained if Windows has not acknowledged cancel,
// so the kernel can never write into a returned stack frame.
struct PipeRequest { OVERLAPPED ov = {}; std::vector<unsigned char> bytes; };
bool Transfer(HANDLE pipe, bool writing, void* data, DWORD length, DWORD timeout) {
  if (!pipe || !length || length > FEED_PIPE_BYTES) return false;
  PipeRequest* p = new PipeRequest;
  p->bytes.resize(length);
  if (writing) memcpy(p->bytes.data(), data, length);
  p->ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!p->ov.hEvent) { delete p; return false; }
  const ULONGLONG deadline = GetTickCount64() + timeout;
  DWORD offset = 0; bool ok = true;
  while (offset < length) {
    ResetEvent(p->ov.hEvent);
    DWORD got = 0;
    BOOL immediate = writing
      ? WriteFile(pipe, p->bytes.data() + offset, length - offset, &got, &p->ov)
      : ReadFile(pipe, p->bytes.data() + offset, length - offset, &got, &p->ov);
    if (!immediate && GetLastError() == ERROR_IO_PENDING) {
      const ULONGLONG now = GetTickCount64();
      DWORD remaining = now < deadline ? (DWORD)(deadline - now) : 0;
      if (WaitForSingleObject(p->ov.hEvent, remaining) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &p->ov);
        if (WaitForSingleObject(p->ov.hEvent, 2000) != WAIT_OBJECT_0) {
          // Intentional tiny leak: an outstanding kernel request owns p.
          Trace("pipe cancellation did not retire; request storage pinned");
          return false;
        }
        ok = false; break;
      }
      if (!GetOverlappedResult(pipe, &p->ov, &got, FALSE)) { ok = false; break; }
    } else if (!immediate) { ok = false; break; }
    if (!got) { ok = false; break; }
    offset += got;
  }
  if (ok && !writing) memcpy(data, p->bytes.data(), length);
  CloseHandle(p->ov.hEvent); delete p; return ok;
}
bool Send(State& s, const void* p, DWORD n, DWORD ms) {
  return Transfer(s.pipe, true, const_cast<void*>(p), n, ms);
}
bool Receive(State& s, void* p, DWORD n, DWORD ms) { return Transfer(s.pipe, false, p, n, ms); }

// DXVK's interop calls take no device lock, and VCRender's host also uses the device from its frame-copier thread
// (GetData flush, LockRect). Every interop call here runs inside the interop's LockDevice/UnlockDevice (VCRender, 2026-11-13:
// without it a TransitionTextureLayout raced the copier and crashed DXVK). Never nested; never held across a GPU wait.
struct DeviceLock {
  ID3D9VkInteropDevice* i = nullptr;
  explicit DeviceLock(IDirect3DDevice9* d) { if (d && SUCCEEDED(d->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**)&i)) && i) i->LockDevice(); else i = nullptr; }
  ~DeviceLock() { if (i) { i->UnlockDevice(); i->Release(); } }
  DeviceLock(const DeviceLock&) = delete; DeviceLock& operator=(const DeviceLock&) = delete;
};
bool GetImage(IDirect3DTexture9* texture, Image& i) {
  if (!texture) return false;
  ID3D9VkInteropTexture* it = nullptr;
  if (FAILED(texture->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) || !it) return false;
  const HRESULT hr = it->GetVulkanImageInfo(&i.image, &i.layout, &i.info);
  it->Release();
  return SUCCEEDED(hr) && i.image && i.info.imageType == VK_IMAGE_TYPE_2D &&
    i.info.samples == VK_SAMPLE_COUNT_1_BIT && i.info.arrayLayers == 1;
}
DXGI_FORMAT ColorFormat(VkFormat f) {
  switch (f) {
  case VK_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
  case VK_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
  case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return DXGI_FORMAT_R10G10B10A2_UNORM;
  case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return DXGI_FORMAT_R11G11B10_FLOAT;
  default: return DXGI_FORMAT_UNKNOWN;
  }
}
DWORD Budget(const State& s) {
  const ULONGLONG now = GetTickCount64();
  return now < s.deadline ? (DWORD)(s.deadline - now) : 0;
}
bool Retired(State& s, DWORD ms) {
  return !s.submitted || FeedVkWaitTimeline(&s.vk, s.retired, s.submitted, ms);
}
// No queue/submission lock may be held on entry. A forced child termination or
// unfinished local work keeps all shared GPU objects alive in quarantine.
bool Destroy(State* s, bool bounded = false) {
  if (!s) return true;
  AcquireSRWLockExclusive(&watchLock); if (watched == s) watched = nullptr; ReleaseSRWLockExclusive(&watchLock);
  if (s->fast && s->helper && WaitForSingleObject(s->helper, 0) == WAIT_OBJECT_0) Unstick(*s, "the helper exited before teardown");
  const DWORD drain = bounded ? (Budget(*s) < 2000 ? Budget(*s) : 2000) : 2000;
  bool retiredOk = Retired(*s, drain);
  if (!retiredOk && s->fast) { Unstick(*s, "teardown found frames still waiting for the helper"); retiredOk = Retired(*s, 1000); }
  bool safe = !s->poisoned && retiredOk;
  if (s->pipe) { CancelIoEx(s->pipe, nullptr); CloseHandle(s->pipe); s->pipe = nullptr; }
  if (s->helper) {
    const DWORD grace = bounded ? (Budget(*s) < 5000 ? Budget(*s) : 5000) : 5000;
    if (WaitForSingleObject(s->helper, grace) != WAIT_OBJECT_0) {
      TerminateProcess(s->helper, 3);
      WaitForSingleObject(s->helper, bounded ? (Budget(*s) < 1000 ? Budget(*s) : 1000) : 1000); safe = false;
    }
    CloseHandle(s->helper); s->helper = nullptr;
  }
  if (!safe) {
    s->poisoned = true; quarantined.push_back(s);
    Trace("teardown could not prove GPU retirement; state pinned until process exit"); return false;
  }
  for (unsigned i = 0; i < FEED_SLOTS; ++i) {
    if (s->shared[i]) s->vk.DestroyImage(s->vkDevice, s->shared[i], nullptr);
    if (s->memory[i]) s->vk.FreeMemory(s->vkDevice, s->memory[i], nullptr);
    if (s->imageHandles[i]) CloseHandle(s->imageHandles[i]);
  }
  if (s->pool) s->destroyPool(s->vkDevice, s->pool, nullptr);
  if (s->retired) s->vk.DestroySemaphore(s->vkDevice, s->retired, nullptr);
  if (s->semIn) s->vk.DestroySemaphore(s->vkDevice, s->semIn, nullptr);
  if (s->semOut) s->vk.DestroySemaphore(s->vkDevice, s->semOut, nullptr);
  if (s->interop) s->interop->Release();
  // FeedVkLoad's server has no remote module and owns this LoadLibrary reference.
  if (s->vk.lib) FreeLibrary(s->vk.lib);
  delete s; return true;
}
bool InitVulkan(State& s, IDirect3DDevice9* device) {
  if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**)&s.interop)) || !s.interop) return false;
  s.device = device;
  s.interop->GetVulkanHandles(&s.instance, &s.physical, &s.vkDevice);
  uint32_t index = 0; s.interop->GetSubmissionQueue(&s.queue, &index, &s.family);
  if (!s.vkDevice || !s.queue) return false;
  const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
  const char* missing = nullptr;
  if (!FeedVkLoad(&s.vk, s.vkDevice, &missing, wine) || !FeedVkHasTimelineQueries(&s.vk)) return false;
  s.gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(s.vk.lib, "vkGetInstanceProcAddr");
  if (!s.gipa) return false;
#define VCB_DEV(member, name) s.member = (PFN_vk##name)s.vk.GetDeviceProcAddr(s.vkDevice, "vk" #name)
  VCB_DEV(queueSubmit, QueueSubmit); VCB_DEV(createPool, CreateCommandPool);
  VCB_DEV(destroyPool, DestroyCommandPool); VCB_DEV(allocateBuffers, AllocateCommandBuffers);
  VCB_DEV(begin, BeginCommandBuffer); VCB_DEV(end, EndCommandBuffer);
  VCB_DEV(reset, ResetCommandBuffer); VCB_DEV(clear, CmdClearColorImage);
#undef VCB_DEV
  s.formatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)s.gipa(s.instance, "vkGetPhysicalDeviceFormatProperties");
  if (!s.queueSubmit || !s.createPool || !s.destroyPool || !s.allocateBuffers || !s.begin || !s.end ||
      !s.reset || !s.clear || !s.formatProperties) return false;
  VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = s.family;
  if (s.createPool(s.vkDevice, &pci, nullptr, &s.pool) != VK_SUCCESS) return false;
  VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  ai.commandPool = s.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
  if (s.allocateBuffers(s.vkDevice, &ai, &s.cb) != VK_SUCCESS) return false;
  ai.commandBufferCount = State::kRing * 2;
  if (s.allocateBuffers(s.vkDevice, &ai, &s.ring[0][0]) != VK_SUCCESS) return false;
  s.signal = (PFN_vkSignalSemaphore)s.vk.GetDeviceProcAddr(s.vkDevice, "vkSignalSemaphore");
  if (!s.signal) s.signal = (PFN_vkSignalSemaphore)s.vk.GetDeviceProcAddr(s.vkDevice, "vkSignalSemaphoreKHR");
  HANDLE unused = nullptr;
  return FeedVkCreateTimeline(&s.vk, false, &s.retired, &unused) >= 0;
}
bool StartHelper(State& s, const VCBConfig& c) {
  std::wstring exe, vendor;
  if (!Wide(c.helper_path, exe) || (c.vendor_dir[0] && !Wide(c.vendor_dir, vendor))) return false;
  const size_t slash = exe.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  const std::wstring directory = exe.substr(0, slash);
  const std::wstring log = directory + L"\\VCBridge-upscale-helper.log";
  wchar_t options[256];
  _snwprintf_s(options, 256, _TRUNCATE,
    L" %lu --backend=%d --preset=%d --fsr_version=%d --sync=1 --log=1 --logpath=",
    (unsigned long)GetCurrentProcessId(), c.backend, c.preset, c.fsr_version);
  std::wstring cmd = Quote(exe) + options + Quote(log);
  if (!vendor.empty()) cmd += L" --vendor-dir=" + Quote(vendor);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end()); mutableCmd.push_back(0);
  STARTUPINFOW si = {}; si.cb = sizeof(si); PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, directory.c_str(), &si, &pi)) return false;
  CloseHandle(pi.hThread); s.helper = pi.hProcess;
  char pipeName[128]; _snprintf_s(pipeName, sizeof(pipeName), _TRUNCATE,
    FEED_PIPE_FMT, (unsigned long)GetCurrentProcessId());
  const ULONGLONG deadline = s.deadline;
  do {
    HANDLE pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) { s.pipe = pipe; break; }
    if (WaitForSingleObject(s.helper, 1) == WAIT_OBJECT_0) return false;
    Sleep(2);
  } while (GetTickCount64() < deadline);
  if (!s.pipe) return false;
  HANDLE selfInHost = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), s.helper, &selfInHost,
      PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) return false;
  FeedHello hello = { FEED_IPC_MAGIC, FEED_IPC_VERSION, GetCurrentProcessId(), FEED_CLIENT_VULKAN,
    (uint64_t)(uintptr_t)selfInHost };
  strcpy_s(hello.build, VCB_HELPER_BUILD_ID);
  auto props = (PFN_vkGetPhysicalDeviceProperties2)s.gipa(s.instance, "vkGetPhysicalDeviceProperties2");
  if (!props) props = (PFN_vkGetPhysicalDeviceProperties2)s.gipa(s.instance, "vkGetPhysicalDeviceProperties2KHR");
  if (props) {
    VkPhysicalDeviceIDProperties id = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 p = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 }; p.pNext = &id;
    props(s.physical, &p); if (id.deviceLUIDValid) memcpy(hello.luid, id.deviceLUID, sizeof(hello.luid));
  }
  FeedHelloAck ack = {};
  return Send(s, &hello, sizeof(hello), Budget(s)) && Receive(s, &ack, sizeof(ack), Budget(s)) &&
    ack.magic == FEED_IPC_MAGIC && ack.version == FEED_IPC_VERSION &&
    !(ack.caps & (FEED_CAP_REFUSED | FEED_CAP_BUILD_MISMATCH));
}
bool Build(State& s, const VCBConfig& c, const VCBFrame& f, const Image& scene) {
  FeedBuild b = {};
  b.width = f.render_width; b.height = f.render_height;
  b.target_width = f.output_width; b.target_height = f.output_height;
  b.color_fmt = ColorFormat(scene.info.format); b.output_fmt = b.color_fmt;
  b.hdr = scene.info.format == VK_FORMAT_R16G16B16A16_SFLOAT || c.hdr;
  b.depth_inverted = c.depth_inverted; b.flags_override = c.flags_override;
  b.transport = c.transport; b.mv_scale_x = f.mv_scale_x; b.mv_scale_y = f.mv_scale_y;
  // the two semaphores the helper opens as its fences (FAST mode); without them the CPU bridge carries the frames
  HANDLE hin = nullptr, hout = nullptr;
  if (s.signal && FeedVkCreateTimeline(&s.vk, true, &s.semIn, &hin) > 0 && FeedVkCreateTimeline(&s.vk, true, &s.semOut, &hout) > 0 && hin && hout) {
    HANDLE di = nullptr, dO = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), hin, s.helper, &di, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
        DuplicateHandle(GetCurrentProcess(), hout, s.helper, &dO, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      b.sem_in = (uint64_t)(uintptr_t)di; b.sem_out = (uint64_t)(uintptr_t)dO;
    } else if (di) DuplicateHandle(s.helper, di, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
  }
  if (hin) CloseHandle(hin);
  if (hout) CloseHandle(hout);
  // The wire is one tag then the packed body, without compiler wrapper padding.
  BYTE tag = 'B'; FeedBuildAck ack = {};
  if (!Send(s, &tag, 1, Budget(s)) || !Send(s, &b, sizeof(b), Budget(s)) ||
      !Receive(s, &ack, sizeof(ack), Budget(s))) return false;
  for (unsigned i = 0; i < FEED_SLOTS; ++i) s.imageHandles[i] = (HANDLE)(uintptr_t)ack.tex[i];
  s.detail = ack.flags;
  if (!ack.ok) return false;
  s.backend = (ack.flags & FEED_ACK_BACKEND_XESS) ? 2 : (ack.flags & FEED_ACK_BACKEND_FSR) ? 1 : 0;
  s.outputFormat = FeedVkFormat((DXGI_FORMAT)(ack.output_fmt ? ack.output_fmt : b.output_fmt));
  const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
  VkExternalMemoryHandleTypeFlagBits type = wine ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT :
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
  for (unsigned i = 0; i < FEED_SLOTS; ++i) {
    const VkFormat format = i == FEED_COLOR ? scene.info.format : i == FEED_OUTPUT ? s.outputFormat :
      i == FEED_DEPTH ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R16G16_SFLOAT;
    const UINT w = i == FEED_OUTPUT ? f.output_width : f.render_width;
    const UINT h = i == FEED_OUTPUT ? f.output_height : f.render_height;
    if (!s.imageHandles[i] || format == VK_FORMAT_UNDEFINED) return false;
    bool ok = FeedVkImportImage(&s.vk, s.imageHandles[i], w, h, format, i == FEED_OUTPUT,
      type, !wine && type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, &s.shared[i], &s.memory[i]);
    if (!ok) {
      const auto other = type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT ?
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
      ok = FeedVkImportImage(&s.vk, s.imageHandles[i], w, h, format, i == FEED_OUTPUT,
        other, !wine && other == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, &s.shared[i], &s.memory[i]);
      if (ok) type = other;
    }
    if (!ok) return false;
  }
  // The probe (DLSS-IV's IvtSyncProbe): a CPU signal of in = 4, 'P', the helper's 'R' says it opened both and saw it; the
  // helper signals out = 4, which must arrive here. Then 'M': FAST (1) or BRIDGE (2, the CPU relays each frame).
  bool fast = false;
  if (b.sem_in && b.sem_out) {
    VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO }; si.semaphore = s.semIn; si.value = kProbeN;
    const VkResult sr = s.signal(s.vkDevice, &si);
    FeedProbeIvt probe = { 'P', kProbeN }; FeedAnswerIvt a = {};
    if (!Send(s, &probe, sizeof(probe), Budget(s)) || !Receive(s, &a, sizeof(a), Budget(s)) || a.tag != 'R' || a.n != kProbeN) return false;
    fast = sr == VK_SUCCESS && (a.status & FEED_ANS_OPENED) && (a.status & FEED_ANS_IN_OK) &&
           FeedVkWaitTimeline(&s.vk, s.semOut, kProbeN, 1500);
  }
  const char* force = getenv("VCR_UPSCALE_SYNC");
  if (force && !strcmp(force, "bridge")) fast = false;
  FeedModeIvt mode = { 'M', fast ? 1u : 2u };
  if (!Send(s, &mode, sizeof(mode), Budget(s))) return false;
  s.fast = fast;
  if (s.helperFrame < kProbeN) s.helperFrame = kProbeN;
  return true;
}
void Barrier(State& s, VkCommandBuffer cb, const Image& image, VkImageLayout from, VkImageLayout to) {
  VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
  b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image.image; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  s.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    0, 0, nullptr, 0, nullptr, 1, &b);
}
void Acquire(State& s, VkCommandBuffer cb, unsigned mask) {
  if (!s.layoutInit) {
    FeedVkBarrierN(&s.vk, cb, s.shared, FEED_SLOTS, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    VkImage release[FEED_SLOTS]; unsigned n = 0;
    for (unsigned i = 0; i < FEED_SLOTS; ++i) if (!(mask & (1u << i))) {
      release[n++] = s.shared[i]; s.released[i] = true;
    }
    FeedVkExternalTransferN(&s.vk, cb, release, n, s.family, true);
    s.layoutInit = true;
  }
  VkImage fromExternal[FEED_SLOTS], same[FEED_SLOTS]; unsigned a = 0, b = 0;
  for (unsigned i = 0; i < FEED_SLOTS; ++i) if (mask & (1u << i)) {
    if (s.released[i]) { fromExternal[a++] = s.shared[i]; s.released[i] = false; }
    else same[b++] = s.shared[i];
  }
  FeedVkExternalTransferN(&s.vk, cb, fromExternal, a, s.family, false);
  FeedVkBarrierN(&s.vk, cb, same, b, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
}
void Release(State& s, VkCommandBuffer cb, unsigned mask) {
  VkImage images[FEED_SLOTS]; unsigned n = 0;
  for (unsigned i = 0; i < FEED_SLOTS; ++i) if ((mask & (1u << i)) && !s.released[i]) {
    images[n++] = s.shared[i]; s.released[i] = true;
  }
  FeedVkExternalTransferN(&s.vk, cb, images, n, s.family, true);
}
bool Begin(State& s) {
  if (!Retired(s, Budget(s))) { s.poisoned = true; return false; }
  if (s.reset(s.cb, 0) != VK_SUCCESS) return false;
  VkCommandBufferBeginInfo b = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  return s.begin(s.cb, &b) == VK_SUCCESS;
}
bool SubmitAndWait(State& s) {
  if (s.end(s.cb) != VK_SUCCESS) return false;
  const uint64_t value = s.submitted + 1;
  VkTimelineSemaphoreSubmitInfo timeline = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
  timeline.signalSemaphoreValueCount = 1; timeline.pSignalSemaphoreValues = &value;
  VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO }; submit.pNext = &timeline;
  submit.commandBufferCount = 1; submit.pCommandBuffers = &s.cb;
  submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &s.retired;
  // Only the immediate queue submit is locked. No IPC, GPU wait or D3D9 call
  // occurs while holding DXVK's submission queue lock.
  s.interop->LockSubmissionQueue();
  const VkResult result = s.queueSubmit(s.queue, 1, &submit, VK_NULL_HANDLE);
  s.interop->ReleaseSubmissionQueue();
  if (result != VK_SUCCESS) { s.poisoned = true; return false; }
  s.submitted = value;
  if (!Retired(s, Budget(s))) { s.poisoned = true; return false; }
  return true;
}
void CopyIn(State& s, VkCommandBuffer cb, const Image& src, unsigned slot, UINT w, UINT h) {
  const VkImageLayout transfer = src.layout == VK_IMAGE_LAYOUT_GENERAL ? src.layout : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  Barrier(s, cb, src, src.layout, transfer);
  FeedVkCopyImage(&s.vk, cb, src.image, transfer, s.shared[slot], VK_IMAGE_LAYOUT_GENERAL, w, h);
  Barrier(s, cb, src, transfer, src.layout);
}
// The copy-out, recorded into cb (both modes).
void CopyOut(State& s, VkCommandBuffer cb, const Image& home, const VCBFrame& f) {
  Acquire(s, cb, kOutputMask);
  const VkImageLayout target = home.layout == VK_IMAGE_LAYOUT_GENERAL ? home.layout : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  Barrier(s, cb, home, home.layout, target);
  if (s.outputFormat == home.info.format)
    FeedVkCopyImage(&s.vk, cb, s.shared[FEED_OUTPUT], VK_IMAGE_LAYOUT_GENERAL, home.image, target, f.output_width, f.output_height);
  else FeedVkBlitImage(&s.vk, cb, s.shared[FEED_OUTPUT], VK_IMAGE_LAYOUT_GENERAL, home.image, target, f.output_width, f.output_height);
  Barrier(s, cb, home, target, home.layout);   // dst ALL_COMMANDS: DXVK's later work on the queue waits for this copy
  Release(s, cb, kOutputMask);
}
void CopyInAll(State& s, VkCommandBuffer cb, const Image& scene, const Image& depth, const Image& mv, bool guides, const VCBFrame& f) {
  Acquire(s, cb, kInputMask);
  CopyIn(s, cb, scene, FEED_COLOR, f.render_width, f.render_height);
  if (guides) {
    CopyIn(s, cb, depth, FEED_DEPTH, f.render_width, f.render_height);
    CopyIn(s, cb, mv, FEED_MV, f.render_width, f.render_height);
  } else {
    VkClearColorValue zero = {}; VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    s.clear(cb, s.shared[FEED_DEPTH], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    s.clear(cb, s.shared[FEED_MV], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
  }
  Release(s, cb, kInputMask);
}
// One submit with optional timeline wait and signals; only the vkQueueSubmit holds DXVK's queue lock.
bool SubmitTimeline(State& s, VkCommandBuffer cb, VkSemaphore waitSem, uint64_t waitValue, VkSemaphore sig0, uint64_t v0, VkSemaphore sig1, uint64_t v1) {
  VkSemaphore sigs[2]; uint64_t vals[2]; uint32_t ns = 0;
  if (sig0) { sigs[ns] = sig0; vals[ns++] = v0; }
  if (sig1) { sigs[ns] = sig1; vals[ns++] = v1; }
  const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
  tl.waitSemaphoreValueCount = waitSem ? 1 : 0; tl.pWaitSemaphoreValues = &waitValue;
  tl.signalSemaphoreValueCount = ns; tl.pSignalSemaphoreValues = vals;
  VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.pNext = &tl;
  si.waitSemaphoreCount = waitSem ? 1 : 0; si.pWaitSemaphores = &waitSem; si.pWaitDstStageMask = &stage;
  si.commandBufferCount = 1; si.pCommandBuffers = &cb; si.signalSemaphoreCount = ns; si.pSignalSemaphores = sigs;
  s.interop->LockSubmissionQueue();
  const VkResult r = s.queueSubmit(s.queue, 1, &si, VK_NULL_HANDLE);
  s.interop->ReleaseSubmissionQueue();
  return r == VK_SUCCESS;
}
bool BeginCb(State& s, VkCommandBuffer cb) {
  if (s.reset(cb, 0) != VK_SUCCESS) return false;
  VkCommandBufferBeginInfo b = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  return s.begin(cb, &b) == VK_SUCCESS;
}
// DXVK can hold a D3D9 Clear of an image as a deferred clear and run it only when DXVK next
// uses that image, which is after (and over) this service's out-of-band Vulkan copies. A
// same-layout transition makes DXVK's context end its render pass and run pending clears
// first. It is queued on DXVK's CS thread, so it must precede FlushRenderingCommands.
void Settle(State& s, IDirect3DTexture9* texture, const Image& i) {
  ID3D9VkInteropTexture* it = nullptr;
  if (!texture || FAILED(texture->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) || !it) return;
  const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, i.info.mipLevels, 0, i.info.arrayLayers };
  s.interop->TransitionTextureLayout(it, &range, i.layout, i.layout);
  it->Release();
}
bool CanBlit(State& s, VkFormat source, VkFormat target) {
  VkFormatProperties a = {}, b = {};
  s.formatProperties(s.physical, source, &a); s.formatProperties(s.physical, target, &b);
  return (a.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
    (b.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
}
bool SameBuild(const State& s, const VCBConfig& c, const VCBFrame& f, const Image& scene, const Image& home) {
  return memcmp(&s.config, &c, sizeof(c)) == 0 && s.shape.render_width == f.render_width &&
    s.shape.render_height == f.render_height && s.shape.output_width == f.output_width &&
    s.shape.output_height == f.output_height && s.shape.mv_scale_x == f.mv_scale_x &&
    s.shape.mv_scale_y == f.mv_scale_y && s.sceneFormat == scene.info.format && s.homeFormat == home.info.format;
}
} // namespace

HRESULT VCB_ServerUpscaleFrame(const VCBConfig& c, const VCBFrame& f,
    IDirect3DDevice9* device, IDirect3DTexture9* sceneTexture, IDirect3DTexture9* homeTexture,
    IDirect3DTexture9* depthTexture, IDirect3DTexture9* mvTexture, VCBResult& result) {
  std::lock_guard<std::mutex> guard(serviceMutex);
  VCB_InitResult(result, E_FAIL); result.sequence = f.sequence;
  const ULONGLONG deadline = GetTickCount64() + c.timeout_ms;
  if (active) active->deadline = deadline;
  if (!VCB_ValidConfig(c) || !VCB_ValidFrame(f) || !device || !sceneTexture || !homeTexture)
    return Fail(result, E_INVALIDARG, "invalid upscale ABI, device or scene/home input");
  // sharpness is handled by the retained game-side RCAS pass after this copy.
  // The native path deliberately does not use the old remote interop/presenter
  // cache. Query the REAL device identity on every call, including reused COM
  // addresses. A quarantined device remains disabled for this server process.
  ID3D9VkInteropDevice* probe = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**)&probe)) || !probe)
    return Fail(result, E_NOINTERFACE, "server DXVK device has no Vulkan interop");
  VkDevice current = VK_NULL_HANDLE; VkInstance instance = VK_NULL_HANDLE; VkPhysicalDevice physical = VK_NULL_HANDLE;
  probe->GetVulkanHandles(&instance, &physical, &current); probe->Release();
  for (State* old : quarantined) if (old->vkDevice == current)
    return Fail(result, D3DERR_DEVICELOST, "prior unfinished GPU work quarantined this Vulkan device");
  Image scene, home, depth, mv;
  bool guides;
  {
  DeviceLock lock(device);
  if (!GetImage(sceneTexture, scene) || !GetImage(homeTexture, home) ||
      ColorFormat(scene.info.format) == DXGI_FORMAT_UNKNOWN ||
      scene.info.extent.width < f.render_width || scene.info.extent.height < f.render_height ||
      home.info.extent.width < f.output_width || home.info.extent.height < f.output_height ||
      !(scene.info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !(home.info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      scene.layout == VK_IMAGE_LAYOUT_UNDEFINED || home.layout == VK_IMAGE_LAYOUT_UNDEFINED)
    return Fail(result, D3DERR_INVALIDCALL, "scene/home Vulkan image, format, size or transfer usage is invalid");
  guides = (f.flags & VCB_FRAME_GUIDES_VALID) && GetImage(depthTexture, depth) && GetImage(mvTexture, mv) &&
    depth.info.format == VK_FORMAT_R32_SFLOAT && mv.info.format == VK_FORMAT_R16G16_SFLOAT &&
    depth.info.extent.width >= f.render_width && depth.info.extent.height >= f.render_height &&
    mv.info.extent.width >= f.render_width && mv.info.extent.height >= f.render_height &&
    (depth.info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) && (mv.info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
    depth.layout != VK_IMAGE_LAYOUT_UNDEFINED && mv.layout != VK_IMAGE_LAYOUT_UNDEFINED;
  }
  if (active && (active->device != device || active->vkDevice != current || !SameBuild(*active, c, f, scene, home))) {
    State* old = active; active = nullptr;
    if (!Destroy(old, true)) return Fail(result, D3DERR_DEVICELOST, "old upscale state did not retire; device quarantined");
  }
  if (!active) {
    State* s = new State; s->config = c; s->shape = f; s->deadline = deadline;
    s->sceneFormat = scene.info.format; s->homeFormat = home.info.format;
    if (!InitVulkan(*s, device) || !StartHelper(*s, c) || !Build(*s, c, f, scene)) {
      result.detail = s->detail; Destroy(s, true);
      return Fail(result, E_FAIL, "native64 helper start/build/import failed; inspect private helper log");
    }
    if (s->outputFormat != home.info.format && !CanBlit(*s, s->outputFormat, home.info.format)) {
      Destroy(s, true); return Fail(result, D3DERR_NOTAVAILABLE, "GPU cannot convert helper output to the destination format");
    }
    active = s;
    Trace(s->fast ? "native64 helper and shared set ready: FAST (GPU-side fences, no CPU wait per frame)"
                  : "native64 helper and shared set ready: BRIDGE (the CPU waits for each step)");
    if (s->fast) {
      AcquireSRWLockExclusive(&watchLock); watched = s; ReleaseSRWLockExclusive(&watchLock);
      if (!watchStarted) { watchStarted = true; HANDLE t = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr); if (t) CloseHandle(t); }
    }
  }
  State& s = *active;
  if (f.sequence <= s.sequence) return Fail(result, E_INVALIDARG, "frame sequence did not increase; refusing stale frame");
  // Settle every image this frame copies, flush, then refresh the layouts and record without a submit lock.
  bool refreshed;
  {
    DeviceLock lock(device);
    Settle(s, sceneTexture, scene); Settle(s, homeTexture, home);
    if (guides) { Settle(s, depthTexture, depth); Settle(s, mvTexture, mv); }
    s.interop->FlushRenderingCommands();
    refreshed = GetImage(sceneTexture, scene) && GetImage(homeTexture, home) &&
                (!guides || (GetImage(depthTexture, depth) && GetImage(mvTexture, mv)));
  }
  if (!refreshed) return Fail(result, D3DERR_DEVICELOST, "image refresh failed");
  LARGE_INTEGER t0, t1, qf; QueryPerformanceCounter(&t0);
  FeedTaggedFrameIvt frame = {}; frame.tag = 'F';
  frame.fm.reset = !s.sequence || (f.flags & VCB_FRAME_RESET) || !(f.flags & VCB_FRAME_VALID) || !guides;
  frame.fm.jitter_x = f.jitter_x; frame.fm.jitter_y = f.jitter_y;
  frame.fm.frame_time_ms = f.frame_time_ms < 1000.0f ? f.frame_time_ms : 0.0f;
  frame.fm.camera_near = f.camera_near; frame.fm.fov_y_rad = f.fov_y_rad;
  if (s.fast) {
    // FAST: copy-in signals in = n; the helper's queue waits for it, runs FSR, signals out = n; the copy-out waits for out >= n.
    if (s.unstuck) return Fail(result, D3DERR_DEVICELOST, "FAST sync was released by the watchdog; upscaling stopped");
    if (s.helper && WaitForSingleObject(s.helper, 0) == WAIT_OBJECT_0) {
      Unstick(s, "the helper exited"); return Fail(result, D3DERR_DEVICELOST, "the FSR helper exited");
    }
    const uint64_t n = s.helperFrame + 1;
    const unsigned k = (unsigned)(n % State::kRing);
    const DWORD slotWait = Budget(s) < 3000 ? Budget(s) : 3000;
    if (s.ringValue[k] && !FeedVkWaitTimeline(&s.vk, s.retired, s.ringValue[k], slotWait)) {
      Unstick(s, "a command buffer from four frames ago did not retire");
      return Fail(result, D3DERR_DEVICELOST, "the FSR helper fell four frames behind; upscaling stopped");
    }
    VkCommandBuffer in = s.ring[k][0], out = s.ring[k][1];
    if (!BeginCb(s, in)) return Fail(result, D3DERR_DEVICELOST, "input command buffer could not begin");
    CopyInAll(s, in, scene, depth, mv, guides, f);
    if (s.end(in) != VK_SUCCESS || !SubmitTimeline(s, in, VK_NULL_HANDLE, 0, s.semIn, n, VK_NULL_HANDLE, 0)) {
      s.poisoned = true; return Fail(result, D3DERR_DEVICELOST, "input copy submit failed");
    }
    s.helperFrame = n;
    frame.fm.n = n;
    if (!Send(s, &frame, sizeof(frame), Budget(s))) {
      s.lastFastN = n; Unstick(s, "the frame message could not be sent");
      return Fail(result, HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE), "helper pipe broken; upscaling stopped");
    }
    if (!BeginCb(s, out)) { s.lastFastN = n; Unstick(s, "output command buffer could not begin"); return Fail(result, D3DERR_DEVICELOST, "output command buffer could not begin"); }
    CopyOut(s, out, home, f);
    const uint64_t retire = s.submitted + 1;
    s.lastFastN = n;   // before the submit that waits on it: the watchdog may release it
    if (s.end(out) != VK_SUCCESS || !SubmitTimeline(s, out, s.semOut, n, s.retired, retire, VK_NULL_HANDLE, 0)) {
      Unstick(s, "output copy submit failed"); return Fail(result, D3DERR_DEVICELOST, "output copy submit failed");
    }
    s.submitted = retire; s.ringValue[k] = retire;
  } else {
    // BRIDGE: as before, every step waits on the CPU.
    if (!Begin(s)) return Fail(result, D3DERR_DEVICELOST, "command-buffer retirement failed");
    CopyInAll(s, s.cb, scene, depth, mv, guides, f);
    if (!SubmitAndWait(s)) return Fail(result, D3DERR_DEVICELOST, "input GPU copy did not retire; no helper frame sent");
    frame.fm.n = ++s.helperFrame;
    FeedAnswerIvt answer = {};
    if (!Send(s, &frame, sizeof(frame), Budget(s)) || !Receive(s, &answer, sizeof(answer), Budget(s)) ||
        answer.tag != 'R' || answer.n != frame.fm.n || !(answer.status & 8u)) {
      s.poisoned = true; // helper may still own shared memory after a timeout
      return Fail(result, HRESULT_FROM_WIN32(ERROR_TIMEOUT), "helper did not confirm this frame evaluated and retired; output was not copied");
    }
    if (!Begin(s)) return Fail(result, D3DERR_DEVICELOST, "output command buffer could not begin");
    CopyOut(s, s.cb, home, f);
    if (!SubmitAndWait(s)) return Fail(result, D3DERR_DEVICELOST, "output GPU copy did not retire; device quarantined");
  }
  QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&qf);
  const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qf.QuadPart;
  s.statFrames++; s.statMs += ms; if (ms > s.statMax) s.statMax = ms;
  if (s.statFrames == 600) {
    char m[200]; _snprintf_s(m, sizeof m, _TRUNCATE, "%s: 600 frames, %.2f ms per upscale call on the host (max %.2f)",
                             s.fast ? "FAST" : "BRIDGE", s.statMs / 600.0, s.statMax);
    Trace(m); s.statFrames = 0; s.statMs = 0; s.statMax = 0;
  }
  s.sequence = f.sequence;
  VCB_InitResult(result, S_OK, s.fast ? "native64 upscale queued on the GPU (FAST)" : "native64 same-frame upscale copied home");
  result.sequence = f.sequence; result.backend = s.backend; result.detail = s.detail;
  result.flags = VCB_RESULT_READY | VCB_RESULT_COPIED_HOME; return S_OK;
}

bool VCB_ServerUpscaleShutdown(IDirect3DDevice9* device) {
  std::lock_guard<std::mutex> guard(serviceMutex);
  if (active && (!device || active->device == device)) {
    State* s = active; active = nullptr;
    if (!Destroy(s)) return false;
  }
  for (State* s : quarantined) if (!device || s->device == device) return false;
  return true;
}

bool VCB_ServerUpscaleDeviceQuarantined(IDirect3DDevice9* device) {
  std::lock_guard<std::mutex> guard(serviceMutex);
  if (active && (!device || active->device == device) && active->poisoned) return true;
  for (State* s : quarantined) if (!device || s->device == device) return true;
  return false;
}

void VCB_ServerUpscaleSetLog(void (*log)(const char*)) { gLog = log; }
