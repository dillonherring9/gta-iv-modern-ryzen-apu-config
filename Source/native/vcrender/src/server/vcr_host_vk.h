// Phase 3 host half (vcr_host_vk.cpp): DXVK interop for DLSS-IV's game side and the native FSR service.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <functional>
#include "../shared/vcr_proto.h"
#include "../shared/vcr_vk_wire.h"
#include "../upscale/vc_upscale_wire.h"

struct VkLookup {
  std::function<IDirect3DDevice9*(uint32_t)> device;     // device id -> real device
  std::function<IUnknown*(const VcrSub&)> resource;      // surface / texture image -> real resource
  std::function<IDirect3DTexture9*(uint32_t)> texture;   // texture id -> real texture
  std::function<uint32_t(uint32_t)> deviceOf;            // object id -> owning device id
};
void vkStartSideChannel(const wchar_t* base);
bool vkHandle(const VkLookup& lk, const uint8_t* data, uint32_t len, vcrvk::Writer& reply);   // true: reply is the response
void upscaleHandle(const VkLookup& lk, const uint8_t* payload, uint32_t bytes, VCBResult& result);
bool vkDeviceReset(IDirect3DDevice9* d, uint32_t devId);   // false: quarantine requires process restart
bool vkDeviceGone(IDirect3DDevice9* d, uint32_t devId);
bool vkDeviceQuarantined(IDirect3DDevice9* d);
