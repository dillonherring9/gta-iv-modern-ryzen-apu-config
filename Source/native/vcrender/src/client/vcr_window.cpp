// Fullscreen window lifecycle. Altered implementation informed by DXVK 3.0.2
// d3d9_window.cpp / wsi_window_win32.cpp; see ../third_party/LICENSE-DXVK.txt.
// Rendering stays in the host; this module only manages the game's own HWND.
#include "vcr_client.h"
#include <wchar.h>

namespace vcr {
void setFullscreenDevice(VDevice*, bool);
namespace {
struct Hook {
  VDevice* owner = nullptr;
  WNDPROC original = nullptr;
  bool unicode = false, activeProcessed = false, inactiveProcessed = false;
  unsigned filtering = 0;
  bool manage = false, fullscreen = true;
  RECT bounds = {};
};
static CRITICAL_SECTION hooksLock;
static std::unordered_map<HWND, Hook>* hooks;
struct HooksInit { HooksInit() { InitializeCriticalSection(&hooksLock); hooks = new std::unordered_map<HWND, Hook>(); } };
static HooksInit hooksInit;
struct HooksGuard { HooksGuard() { EnterCriticalSection(&hooksLock); } ~HooksGuard() { LeaveCriticalSection(&hooksLock); } };

static LRESULT CALLBACK fullscreenProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  Hook data;
  bool notify = false;
  {
    HooksGuard guard;
    auto it = hooks->find(window);
    if (it == hooks->end()) return DefWindowProcW(window, message, wp, lp);
    data = it->second;
    if (data.owner && data.fullscreen && !data.filtering && message == WM_ACTIVATEAPP) {
      const bool active = wp != 0;
      notify = active ? !data.activeProcessed : !data.inactiveProcessed;
      it->second.activeProcessed = active;
      it->second.inactiveProcessed = !active;
      // The hook lock protects owner lifetime. This callback only updates atomic
      // cooperative state and must never take the device lock.
      if (notify) data.owner->windowActivated(active);
    }
  }
  if (notify) log("[window] activation hwnd=%p active=%u iconic=%d foreground=%p manage=%d",window,(unsigned)(wp!=0),IsIconic(window),GetForegroundWindow(),data.manage);
  if (data.owner && data.fullscreen && message == WM_NCCALCSIZE && wp) return 0;
  auto def = [&] { return data.unicode ? DefWindowProcW(window,message,wp,lp) : DefWindowProcA(window,message,wp,lp); };
  if (!data.original || (data.owner && data.filtering)) return def();
  if (data.owner && data.fullscreen && data.manage) {
    if (message == WM_ACTIVATEAPP && notify) {
      // Match D3D9's activation bookkeeping, without stealing focus every frame.
      // Borderless uses the monitor rectangle, not the smaller render dimensions.
      {
        HooksGuard guard;
        auto it = hooks->find(window); if (it != hooks->end()) it->second.filtering++;
      }
      if (wp) {
        const RECT& r = data.bounds;
        // WM_SIZE can have queued a synthetic activation before focus left.
        // Only the actual foreground game may raise its borderless window.
        const bool raiseBorderless=fullscreenSetting()==0 && GetForegroundWindow()==window;
        SetWindowPos(window,raiseBorderless?HWND_TOPMOST:nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,
                     SWP_NOACTIVATE|(raiseBorderless?0:SWP_NOZORDER));
      } else if (fullscreenSetting()==1 && IsWindowVisible(window)) {
        // Only exclusive presentation has a minimize/restore lifecycle. The
        // default GDI borderless target must stay mapped through launcher focus
        // handoffs, otherwise Gamescope can only show Wine's minimized icon.
        ShowWindow(window,SW_MINIMIZE);
      } else if (fullscreenSetting()==0) {
        // Keep the drawable mapped, while allowing the newly focused desktop
        // application to appear above it during a normal alt-tab.
        SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
      }
      {
        HooksGuard guard;
        auto it = hooks->find(window); if (it != hooks->end() && it->second.filtering) it->second.filtering--;
      }
    } else if (message == WM_SIZE && !IsIconic(window)) {
      PostMessageW(window,WM_ACTIVATEAPP,TRUE,GetCurrentThreadId());
    }
  }
  LRESULT result = data.unicode ? CallWindowProcW(data.original,window,message,wp,lp)
                               : CallWindowProcA(data.original,window,message,wp,lp);
  if (message == WM_NCDESTROY) {
    HooksGuard guard;
    hooks->erase(window);
  }
  return result;
}

static bool hookWindow(VDevice* owner, HWND window, const RECT& bounds, bool manage) {
  HooksGuard guard;
  auto it = hooks->find(window);
  if (it != hooks->end()) {
    // A later application subclass may still chain through us. Reuse that node
    // rather than inserting our proc a second time and making a call cycle.
    if (it->second.owner && it->second.owner != owner) return false;
    it->second.owner=owner; it->second.bounds=bounds; it->second.manage=manage; it->second.fullscreen=true;
    return true;
  }
  Hook data; data.owner=owner; data.unicode=IsWindowUnicode(window); data.bounds=bounds; data.manage=manage;
  SetLastError(0);
  data.original=(WNDPROC)(data.unicode ? SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)fullscreenProc)
                                     : SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)fullscreenProc));
  if (!data.original) { log("[window] subclass failed hwnd=%p error=%lu",window,GetLastError()); return false; }
  hooks->emplace(window,data);
  return true;
}
static void unhookWindow(VDevice* owner, HWND window) {
  HooksGuard guard;
  auto it=hooks->find(window); if(it==hooks->end() || it->second.owner!=owner) return;
  Hook& data=it->second;
  WNDPROC current=(WNDPROC)(data.unicode ? GetWindowLongPtrW(window,GWLP_WNDPROC) : GetWindowLongPtrA(window,GWLP_WNDPROC));
  if (!IsWindow(window) || current==fullscreenProc) {
    if (IsWindow(window)) {
      if(data.unicode) SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)data.original);
      else SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)data.original);
    }
    hooks->erase(it);
  } else {
    // Preserve another plugin's subclass chain after our device has gone away.
    data.owner=nullptr; data.manage=false; data.filtering=0;
  }
}
struct WindowFilter {
  HWND window;
  explicit WindowFilter(HWND w):window(w) { HooksGuard guard; auto it=hooks->find(w); if(it!=hooks->end())it->second.filtering++; }
  ~WindowFilter() { HooksGuard guard; auto it=hooks->find(window); if(it!=hooks->end() && it->second.filtering)it->second.filtering--; }
};

// Wine's private escape pins EXCLUSIVE presentation geometry. It also changes
// GDI child clipping, so do not enable it for the default GDI borderless mode.
struct WineEscape { UINT adapter, device, type, flags; void* data; UINT bytes, context; };
static void winePresentRect(HWND window, const RECT& rect) {
  HMODULE gdi=GetModuleHandleW(L"gdi32.dll");
  auto escape=gdi ? (LONG (WINAPI*)(const WineEscape*))GetProcAddress(gdi,"D3DKMTEscape") : nullptr;
  if(!escape) return;
  RECT r=rect;
  WineEscape args={0,0,0x80000001u,0,&r,sizeof r,(UINT)(uintptr_t)window};
  LONG status=escape(&args);
  log("[window] Wine exclusive present rect hwnd=%p %ld,%ld %ldx%ld status=%08lx",window,r.left,r.top,r.right-r.left,r.bottom-r.top,(unsigned long)status);
}
}

void VDevice::applyWindowMode(const D3DPRESENT_PARAMETERS& pp, const D3DDISPLAYMODEEX* fsm) {
  if(pp.Windowed) { leaveFullscreen(); return; }
  HWND window=pp.hDeviceWindow;
  if(!window || !IsWindow(window)) return;
  // Capture the intended target before restoring the previous fullscreen state:
  // leaving fullscreen may move this same HWND back to its original monitor.
  HMONITOR monitor=MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY);
  MONITORINFOEXW mi={}; mi.cbSize=sizeof mi;
  if(!GetMonitorInfoW(monitor,&mi)) return;
  if(fsActive && (fsWindow!=window || _wcsicmp(fsDisplay,mi.szDevice)!=0)) leaveFullscreen();
  const bool entering=!fsActive;
  if(entering) {
    fsWindow=window; fsWindowManaged=!(cp.BehaviorFlags&D3DCREATE_NOWINDOWCHANGES);
    savedStyle=GetWindowLongW(window,GWL_STYLE);savedExStyle=GetWindowLongW(window,GWL_EXSTYLE);GetWindowRect(window,&savedRect);
    wcsncpy(fsDisplay,mi.szDevice,CCHDEVICENAME-1);fsDisplay[CCHDEVICENAME-1]=0;
    savedDisplayMode={};savedDisplayMode.dmSize=sizeof savedDisplayMode;
    savedDisplayValid=EnumDisplaySettingsExW(fsDisplay,ENUM_CURRENT_SETTINGS,&savedDisplayMode,0)!=FALSE;
    fsActive=true;setFullscreenDevice(this,true);
  }
  UINT hz=fsm?fsm->RefreshRate:pp.FullScreen_RefreshRateInHz;
  fsMode.Width=pp.BackBufferWidth;fsMode.Height=pp.BackBufferHeight;fsMode.RefreshRate=hz?hz:60;
  fsMode.Format=pp.BackBufferFormat==D3DFMT_A8R8G8B8?D3DFMT_X8R8G8B8:pp.BackBufferFormat;
  if(!hookWindow(this,window,mi.rcMonitor,fsWindowManaged)) {
    // Nothing has been changed yet. Do not publish a fullscreen owner if this
    // HWND is already owned by another device or subclass installation failed.
    fsActive=false;fsWindow=nullptr;fsWindowManaged=false;savedDisplayValid=false;fsDisplay[0]=0;
    setFullscreenDevice(this,false);
    return;
  }
  if(!fsWindowManaged) return;
  if(entering) {
    HWND focus=cp.hFocusWindow?cp.hFocusWindow:window;
    SetWindowPos(focus,nullptr,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
  }
  WindowFilter filter(window);
  if(fullscreenSetting()==1) {
    DEVMODEW dm={};dm.dmSize=sizeof dm;dm.dmPelsWidth=pp.BackBufferWidth;dm.dmPelsHeight=pp.BackBufferHeight;dm.dmBitsPerPel=32;
    dm.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL;
    if(hz){dm.dmDisplayFrequency=hz;dm.dmFields|=DM_DISPLAYFREQUENCY;}
    LONG result=ChangeDisplaySettingsExW(fsDisplay,&dm,nullptr,CDS_FULLSCREEN,nullptr);
    if(result!=DISP_CHANGE_SUCCESSFUL && hz){dm.dmFields&=~DM_DISPLAYFREQUENCY;result=ChangeDisplaySettingsExW(fsDisplay,&dm,nullptr,CDS_FULLSCREEN,nullptr);}
    modeChanged=modeChanged||result==DISP_CHANGE_SUCCESSFUL;
    log("fullscreen %ux%u@%u: display mode change %s (%ld)",pp.BackBufferWidth,pp.BackBufferHeight,hz,result==DISP_CHANGE_SUCCESSFUL?"ok":"refused",result);
    GetMonitorInfoW(monitor,&mi);
    winePresentRect(window,mi.rcMonitor);fsPresentRect=true;
  } else log("fullscreen %ux%u@%u: borderless window over the %ldx%ld monitor, frame scaled to fit (FullscreenMode=0)",pp.BackBufferWidth,pp.BackBufferHeight,hz,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top);
  hookWindow(this,window,mi.rcMonitor,true);
  LONG style=GetWindowLongW(window,GWL_STYLE)&~WS_OVERLAPPEDWINDOW;
  LONG exstyle=GetWindowLongW(window,GWL_EXSTYLE)&~WS_EX_OVERLAPPEDWINDOW;
  SetWindowLongW(window,GWL_STYLE,style|WS_VISIBLE);SetWindowLongW(window,GWL_EXSTYLE,exstyle);
  const RECT& r=mi.rcMonitor;
  const HWND order=(fullscreenSetting()==1 || GetForegroundWindow()==window)?HWND_TOPMOST:HWND_NOTOPMOST;
  SetWindowPos(window,order,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);
  log("[window] fullscreen hwnd=%p iconic=%d visible=%d foreground=%p",window,IsIconic(window),IsWindowVisible(window),GetForegroundWindow());
}

void VDevice::leaveFullscreen() {
  if(!fsActive) return;
  HWND window=fsWindow;
  // Keep the hook inert while restoring geometry, then detach it independently
  // from the swapchain objects Reset has already replaced.
  {
    { HooksGuard guard; auto it=hooks->find(window); if(it!=hooks->end() && it->second.owner==this)it->second.fullscreen=false; }
    WindowFilter filter(window);
    if(modeChanged) {
      LONG result=ChangeDisplaySettingsExW(fsDisplay,savedDisplayValid?&savedDisplayMode:nullptr,nullptr,0,nullptr);
      log("[window] restore display %ls result=%ld",fsDisplay,result);modeChanged=false;
    }
    if(fsPresentRect) { RECT empty={};winePresentRect(window,empty);fsPresentRect=false; }
    if(fsWindowManaged && IsWindow(window)) {
      const LONG current=GetWindowLongW(window,GWL_STYLE), ex=GetWindowLongW(window,GWL_EXSTYLE);
      const LONG dynamic=WS_VISIBLE|WS_MINIMIZE|WS_MAXIMIZE;
      // Respect application style changes made while fullscreen, as native
      // runtimes do. Preserve current show/minimized state during restoration.
      if((current&~dynamic)==(savedStyle&~(dynamic|WS_OVERLAPPEDWINDOW)) &&
         (ex&~WS_EX_TOPMOST)==(savedExStyle&~(WS_EX_TOPMOST|WS_EX_OVERLAPPEDWINDOW))) {
        SetWindowLongW(window,GWL_STYLE,(savedStyle&~dynamic)|(current&dynamic));
        SetWindowLongW(window,GWL_EXSTYLE,savedExStyle);
      }
      SetWindowPos(window,(savedExStyle&WS_EX_TOPMOST)?HWND_TOPMOST:HWND_NOTOPMOST,
                   savedRect.left,savedRect.top,savedRect.right-savedRect.left,savedRect.bottom-savedRect.top,SWP_FRAMECHANGED|SWP_NOACTIVATE);
    }
  }
  unhookWindow(this,window);
  fsActive=false;fsWindow=nullptr;fsWindowManaged=false;savedDisplayValid=false;fsDisplay[0]=0;
  setFullscreenDevice(this,false);
  log("[window] fullscreen left hwnd=%p",window);
}

// GTA IV's own WndProc also minimizes an exclusive request on activation loss.
// Our GDI borderless presenter has no native swapchain to restore that window.
// Complete this startup handoff once, before the first visible frame; subsequent
// user minimize/alt-tab operations retain their normal lifetime.
void VDevice::restoreStartupWindow(const D3DPRESENT_PARAMETERS& pp) {
  if (pp.Windowed || fullscreenSetting()!=0 || !fsActive || !fsWindowManaged ||
      startupPresented || startupRestoreAttempted || GetTickCount()-startupWindowTick>30000) return;
  HWND window=pp.hDeviceWindow;
  if (!window || window!=fsWindow || (cp.hFocusWindow && cp.hFocusWindow!=window) || !IsIconic(window)) return;
  DWORD pid=0; GetWindowThreadProcessId(window,&pid);
  wchar_t cls[64]={}; GetClassNameW(window,cls,64);
  if (pid!=GetCurrentProcessId() || wcscmp(cls,L"grcWindow") || GetAncestor(window,GA_ROOT)!=window) return;
  MONITORINFO mi={};mi.cbSize=sizeof mi;
  if (!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&mi)) return;
  startupRestoreAttempted=true;
  // Reset may be on DLSS's worker while the main window thread waits for it.
  // Queue both operations instead of synchronously entering that thread.
  const BOOL restored=ShowWindowAsync(window,SW_SHOWNOACTIVATE);
  const RECT& r=mi.rcMonitor;
  const BOOL sized=SetWindowPos(window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,
      SWP_ASYNCWINDOWPOS|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
  log("[window] startup main-window handoff hwnd=%p restore queued=%d fullscreen queued=%d bounds=%ldx%ld foreground=%p",
      window,restored,sized,r.right-r.left,r.bottom-r.top,GetForegroundWindow());
}
} // namespace vcr
