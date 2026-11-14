// The HUD-less picture in SDR: what the mod does at a back-buffer draw, another module's point copy of the back buffer, that
// module's draw after it, and the frame's end, written once over a binding so the mod and the device-thread harness run the
// same lines. The rules are fg_hud_rule.h's.
#pragma once
#include "fg_hud_rule.h"

// One frame's facts (made afresh at the frame's first question by its stamp), the session's latch, and counts.
struct FgHudSdrFrameFacts { unsigned at, bbN; bool iface, phone, copied, intoPre, foreign, curve, notCurve, replayed; };
struct FgHudSdrState
{
    FgHudSdrFrameFacts f;
    bool latch;                          // a curve was told last frame: the copy goes into the curve's copy
    long frames, ifaceFrames, phoneFrames, pics, copies, noCopy, preMiss, curves, replays, replayFails, odd, notCurves, strays, disagree;
};

// The sequence over a binding B. B gives:
//   St()                         the state
//   Stamp()                      this frame's stamp
//   SceneBefore(), Resolved()    a scene draw or the mod's resolve came this frame; the resolve came
//   RtIsBackBuffer()             render target 0 is the real back buffer
//   OpaqueQuad(d)                the draw is an opaque full-screen quad
//   Overlay3d(d, bbBefore, sc)   the draw is a 3D overlay ahead of the interface
//   Copy(d, intoPre)             1 copied; 0 refused; -1 no curve's copy to take it into
//   InFrame()                    between the frame's resolve and its EndScene
//   Arm(c)                       another module's point copy of the whole back buffer: its module and texture kept
//   SetArmed(a)                  the draw hooks ask that module's next draw, or nothing
//   SameModule(m)                the draw is that module's
//   CurveFacts(m)                the curve's facts of the draw
//   Replay(m)                    true where the draw was replayed into the target from the curve's copy
//   Publish(hudDrew, pic)        what the frame has, for FgHudWant
template <class B> struct FgHudSdr
{
    static FgHudSdrFrameFacts& F()
    {
        FgHudSdrState& s = B::St();
        const unsigned at = B::Stamp();
        if (s.f.at != at) { s.f = FgHudSdrFrameFacts(); s.f.at = at; }
        return s.f;
    }
        // Until the interface's first draw: the boundary and the copy for a 2D draw; the overlay rule for a 3D draw that is not the
    // scene's (the phone: no picture).
    template <class D> static void OnDraw(const D& d, bool ortho, bool sceneCam)
    {
        FgHudSdrState& s = B::St(); FgHudSdrFrameFacts& f = F();
        const unsigned bbBefore = f.bbN++;
        if (f.iface || f.phone) return;
        const bool sceneBefore = B::SceneBefore();
        if (ortho)
        {
            if (!sceneBefore) return;                                   // no render state is read ahead of the scene
            if (!FgHudSdrBoundaryRule(true, B::OpaqueQuad(d), sceneBefore, false)) return;
            f.iface = true;                                             // the interface's first draw
            if (!FgHudSdrCopyRule(B::Resolved(), B::RtIsBackBuffer(), f.copied)) { DEVSTAT(s.noCopy++;) return; }
            const bool pre = FgHudSdrIntoPreRule(s.latch);
            const int r = B::Copy(d, pre);
            if (r > 0) { f.copied = true; f.intoPre = pre; DEVSTAT(s.copies++;) }
            DEVSTAT(else { s.noCopy++; if (r < 0) s.preMiss++; })
            return;
        }
        if (sceneCam) return;                                           // the coronas: scene to both pictures
        if (B::Overlay3d(d, bbBefore, sceneBefore)) f.phone = true;     // the phone: no HUD-less picture
    }
        // hkStretchRect, while the session is SDR and the machine installed. Only inside the frame.
    template <class C> static void OnCopy(const C& c)
    {
        FgHudSdrState& s = B::St();
        if (!B::InFrame()) { DEVSTAT(s.strays++;) return; }                      // between EndScene and the next resolve: set aside
        if (!B::Arm(c)) return;
        F().foreign = true;
        B::SetArmed(true);
    }
        // That module's first back-buffer draw after its copy: the curve told, and replayed.
    template <class M> static void OnModuleDraw(const M& m)
    {
        if (!B::SameModule(m)) return;                                  // someone else's draw: the arm stands
        B::SetArmed(false);                                             // one draw an arm
        FgHudSdrState& s = B::St(); FgHudSdrFrameFacts& f = F();
        if (!FgHudSdrCurveRule(B::CurveFacts(m))) { f.notCurve = true; DEVSTAT(s.notCurves++;) return; }
        f.curve = true;
        if (!FgHudSdrReplayRule(f.copied, f.intoPre, true)) return;
        if (B::Replay(m)) { f.replayed = true; DEVSTAT(s.replays++;) }
        DEVSTAT(else s.replayFails++;)
    }
        // hkEndScene, after the encode. asked: this frame's draws were asked, so it has a verdict. The latch is set every frame.
    static void Frame(bool asked)
    {
        FgHudSdrState& s = B::St();
        const FgHudSdrFrameFacts f = F();
        const bool odd = f.foreign && !f.curve;
        if (asked)
        {
            const bool hudDrew = FgHudDrewRule(f.iface ? 1u : 0u, f.phone);
            const bool pic = !f.phone && FgHudSdrPicRule(f.copied, f.intoPre, f.curve, f.replayed, odd);
            DEVSTAT(s.frames++;)
            DEVSTAT(if (hudDrew) s.ifaceFrames++;)
            DEVSTAT(if (f.phone) s.phoneFrames++;)
            DEVSTAT(if (pic) s.pics++;)
            DEVSTAT(if (f.curve) s.curves++;)
            DEVSTAT(if (odd) s.odd++;)
            DEVSTAT(if (f.copied && f.intoPre != f.curve) s.disagree++;)
            B::Publish(hudDrew, pic);
        }
        s.latch = FgHudSdrLatchRule(f.curve);
        B::SetArmed(false);
    }
};
