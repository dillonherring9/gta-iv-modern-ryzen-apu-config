// ds_bracket.h: a save/restore bracket of the mod's, and another thread's depth-stencil bind landing inside it.
// Every full-screen pass of the mod's is a bracket: it asks the device which depth stencil is bound, binds none, draws, and
// puts back what it was told. GTA IV's main thread binds the depth stencil while the render thread draws (as a load ends and
// as the menu comes up), and the brackets sit at the end of the frame, when the main thread wakes. A bind that lands between
// the question and the put-back would be overwritten, and the engine would then read the old surface and keep putting it
// back every frame. With the device thread armed the bracket is wider, since IvtSaveState's GetPixelShaderConstantF drains.
// The rule: what a bracket puts back is the depth stencil the game has bound now. No game code runs on the render thread
// inside a bracket, so the only game bind that can land inside one is another thread's, and hkSetDepthStencil marks those
// here: the sequence is odd from that hook's entry to DXVK's return. A bracket remembers the sequence when it opens; if it
// moved, or was odd, the other thread's surface goes back instead of the saved one, and either order on the device ends on it.
//   A refused bind un-marks itself, so a bracket opened before it began puts its saved surface back; one opened while it was
//   in flight is handed the refused surface. DXVK 3.1 refuses only a surface without depth-stencil usage, which this game
//   never binds. Two other threads binding at once are not told apart: the last one's surface goes back, which is what the
//   device ends on anyway.
// No lock: a lock the other thread held across its wait for the device thread would deadlock against the injection's body,
// which runs on the device thread and is a bracket too.
// Cost: one load where a bracket opens, one load and a compare where it closes.
// Proof: tools/perfproof/dt_test.cpp, BracketForeignBind, and its negative control.
#pragma once

static volatile LONG dsForeignSeq = 0;                          // odd: another thread's SetDepthStencilSurface is between the mod's hook and DXVK's return
static IDirect3DSurface9* volatile dsForeignBound = nullptr;    // what it bound; borrowed, the game owns it
DEVSTAT(static LONG dsBracketsCrossed = 0;)                     // brackets that put the other thread's surface back (the harness's)

static __forceinline void DsForeignBegin(IDirect3DSurface9* ds) { dsForeignBound = ds; _InterlockedIncrement(&dsForeignSeq); }
static __forceinline void DsForeignEnd(HRESULT hr) { if (SUCCEEDED(hr)) _InterlockedIncrement(&dsForeignSeq); else _InterlockedDecrement(&dsForeignSeq); }

static __forceinline LONG DsBracketOpen() { return dsForeignSeq; }

// The engine's own bracket: RAGE switches target by asking which depth stencil is bound, binding none, working, and putting
// back what it was told. If the main thread's bind lands between, the render thread puts back the stale surface and the
// engine restores it every frame from then on: the game's own race, won or lost by microseconds. So the engine's bracket gets
// the rule the mod's have. The render thread's question is noted with the sequence read before it is asked; a bind by the
// render thread of exactly the surface it was told, made after another thread's bind began or landed, is a stale put-back,
// and the other thread's surface goes to the device instead, until the next question. Only on the render thread and never on
// the mod's own binds. Render-thread state, no lock.
static IDirect3DSurface9* dsAsked = nullptr;       // what the render thread was last told is bound
static LONG dsAskedSeq = 0;
static bool dsAskedValid = false;
DEVSTAT(static LONG dsStalePutBacks = 0;)          // engine put-backs that were handed the other thread's surface (the harness's)
static __forceinline void DsAskedNote(IDirect3DSurface9* told, bool ok, LONG seqBeforeAsking) { dsAsked = told; dsAskedSeq = seqBeforeAsking; dsAskedValid = ok; }
static __declspec(noinline) IDirect3DSurface9* DsStaleCorrected() { DEVSTAT(dsStalePutBacks++;) return dsForeignBound; }
static __forceinline IDirect3DSurface9* DsStalePutBack(IDirect3DSurface9* ds)
{
    if (!dsAskedValid || ds != dsAsked) return ds;
    return (dsForeignSeq == dsAskedSeq && !(dsAskedSeq & 1)) ? ds : DsStaleCorrected();
}
static __declspec(noinline) IDirect3DSurface9* DsBracketCrossed() { DEVSTAT(dsBracketsCrossed++;) return dsForeignBound; }
static __forceinline IDirect3DSurface9* DsBracketPutBack(IDirect3DSurface9* saved, LONG seqAtOpen)
{
    return (dsForeignSeq == seqAtOpen && !(seqAtOpen & 1)) ? saved : DsBracketCrossed();
}
