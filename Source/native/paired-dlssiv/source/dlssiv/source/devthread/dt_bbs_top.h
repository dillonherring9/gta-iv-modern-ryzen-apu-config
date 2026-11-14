// Entry points of devthread/dt_bbs.h (whether the back buffer was written since the mark), read only in a build whose product
// can arm (DT_BBS_ON). Declarations only, at the top: a type or static declared in the middle of the translation unit
// reorders the hot statics. Needs <windows.h> and <d3d9.h>.
#pragma once

static __forceinline bool DtBbsLooks(DWORD slot);               // this replayed call is looked at (DtExec's one test)
static void DtBbsSeen(DWORD slot, const DWORD* c, HRESULT hr);  // a replayed call whose byte is set, after DXVK returned
static void DtBbsPresentAhead();                                // a replayed Present, AHEAD of its call
static int  DtBbsEnter(const void* d, int kind, const void* dst);   // a call that reaches DXVK beside the replay, ahead of it: 0, or a token for DtBbsLeave
static void DtBbsLeave(int entered);                            // and behind it, once DXVK has returned
static int  DtBbsPresentBeside(void* sc);                       // a Present of a swap chain beside the replay, ahead of it: 0, or a token for DtBbsLeave
static void DtBbsRtMoved(const void* d, DWORD i, const void* s);    // a SetRenderTarget that reached DXVK beside the replay and was taken
static void DtBbsRawWrite();                                    // the mod's own raw write into the back buffer (the transport's home copy), ahead of it
static void DtBbsAtArm(IDirect3DSwapChain9* sc);                // the layer's arm: the table; no mark stands
static void DtBbsMark();                                        // the mark (B::BackMark): on the worker, where a pair is armed
static bool DtBbsWritten(const void* bb, int fire, bool rect);  // the question (B::BackWritten): on the worker, at a fire
// The calls the tracker follows reach it through the device thread's observers: SetRenderTarget always, the rest while a pair
// is armed (dt_bbs.h).
static void DtBbsObs37(const DWORD* c);                         // dtObs[37] while no checkpoint stands: the binding's bookkeeping
static void DtBbsObs30(const DWORD* c);                         // dtObs[30] while a pair is armed: UpdateSurface
template <DWORD SL> static void DtBbsAt(const DWORD* c);        // inside a checkpoint observer, ahead of its own work: the slot's look
static void DtBbsBindFailed();                                  // DtExec's failed branch, a replayed SetRenderTarget that DXVK refused
