// Native x64 bridge-side temporal upscale service. Project integration, MIT.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include "vc_upscale_wire.h"

// Called only by the bridge's ordered device FIFO. All objects are real,
// server-local DXVK D3D9 interfaces. A successful result contains this frame's
// output, never the previous frame. No Vulkan/GPU handle crosses the x86 RPC.
HRESULT VCB_ServerUpscaleFrame(const VCBConfig&, const VCBFrame&,
    IDirect3DDevice9*, IDirect3DTexture9* scene, IDirect3DTexture9* home,
    IDirect3DTexture9* depth, IDirect3DTexture9* mv, VCBResult&);

// Call BEFORE Reset/ResetEx/final device Release, and at server shutdown.
// nullptr tears down the active device. This function never runs from DllMain.
bool VCB_ServerUpscaleShutdown(IDirect3DDevice9* device);

// A server device with unretired custom GPU work must not be forcibly released
// by the bridge's release-until-zero cleanup. Leave it to process teardown.
bool VCB_ServerUpscaleDeviceQuarantined(IDirect3DDevice9* device);

// VCRender: where the service's lines go (the host log). Optional.
void VCB_ServerUpscaleSetLog(void (*log)(const char*));
