# Renderer review fixtures through 269

These retained source fixtures cover actual D3D9/DXVK resources and isolated
Windows presentation. Compile for x86-windows-gnu with Zig 0.15.2; add
`-I ../src/third_party/vulkan` for quarantine/capture and link `-luser32 -lgdi32
-luuid` as required. Run on a suitable Windows system or isolated Wine/Proton
prefix with real DXVK/Vulkan presentation and the matching complete runtime
dependency set. No test should use a personal game prefix. The public source
export contains no compiled test or vendor runtime DLLs.

- startup_handoff.cpp: native CE-style activation-loss minimize and bounded
  handoff, including ineligible, one-shot and already-presented controls. Its
  optional external-foreground case retains two known Gamescope selection
  differences and is not part of the 38 passing primary assertions.
- borderless_regression.cpp: minimize, activation and z-order behavior.
- retained_swapchain.cpp: retained implicit interface over Reset.
- retained_backbuffer.cpp / reset_surface.cpp / ex_surface.cpp: ordinary/Ex
  surface ownership, Reset rejection/persistence and balanced device references.
- mapping_retry.cpp: exactly one in-memory IAT mapping failure, then recovery.
- quarantine.cpp: FAST/BRIDGE helper death after retired work, truthful shutdown
  failure, rejected Reset and bounded client release. It terminates only the
  fixture's exact helper descendant. It does not certify arbitrary GPU-hang recovery.
- capture_slot.cpp: includes the actual presenter and pauses a shared read with
  a guarded page to verify pinning against two forced host slot selections.

The ordinary D3D9 fixtures accept a DLL path and a result path; see each main
function for additional arguments. quarantine follows phase3_test's package
layout and mode selection. The capture fixture accepts an optional result path.
Retained installed269 results are reported separately in SOURCE-STATUS.md.
