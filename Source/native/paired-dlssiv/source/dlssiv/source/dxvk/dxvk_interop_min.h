// Minimal, self-contained declaration of the two DXVK D3D9 Vulkan-interop
// interfaces we use.
//
// WHY NOT DXVK'S OWN HEADER: d3d9_interfaces.h includes "d3d9_include.h" and
// "../vulkan/vulkan_loader.h", both internal to the DXVK tree, and transitively
// needs the Vulkan SDK. Dragging both in for two interfaces and four Vulkan
// symbols would be a large dependency for a small need.
//
// THE PART THAT MUST BE EXACT: these are COM interfaces, so the method order
// below IS the vtable layout. It was transcribed from DXVK's own header (see
// d3d9_interfaces.h in this folder), not from memory, and the unused methods
// are kept as placeholders precisely because deleting them would silently
// shift every later slot.
//
// THE OTHER PART THAT MUST BE EXACT: on 32-bit Windows a Vulkan
// NON-dispatchable handle is a uint64_t, not a pointer. VkImage is
// non-dispatchable, so it is 8 bytes here even though the process is 32-bit.
// Getting that wrong would corrupt the stack on the very first call. Dispatchable
// handles (VkDevice, VkInstance, VkPhysicalDevice, VkQueue) ARE pointers.

#pragma once

#include <windows.h>
#include <stdint.h>
#include <d3d9.h>

// --- the minimum Vulkan surface area, matching vulkan_core.h exactly --------
//
// When the real vulkan_core.h is included first (the transport needs the full
// header), these stand-ins step aside: the values are the same, the typedefs
// would collide.
#ifndef VULKAN_CORE_H_

typedef uint64_t VkImage;                 // non-dispatchable: uint64 on x86
#define VK_NULL_HANDLE 0
typedef struct VkDevice_T*         VkDevice;
typedef struct VkInstance_T*       VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkQueue_T*          VkQueue;

typedef int32_t  VkImageLayout;
typedef uint32_t VkImageAspectFlags;
typedef struct VkImageCreateInfo VkImageCreateInfo;   // opaque; we pass nullptr

#define VK_IMAGE_LAYOUT_UNDEFINED                 0
#define VK_IMAGE_LAYOUT_GENERAL                   1
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL  2
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  5
#define VK_IMAGE_ASPECT_COLOR_BIT                 0x00000001

typedef struct VkImageSubresourceRange {
    VkImageAspectFlags aspectMask;
    uint32_t           baseMipLevel;
    uint32_t           levelCount;
    uint32_t           baseArrayLayer;
    uint32_t           layerCount;
} VkImageSubresourceRange;

#endif // VULKAN_CORE_H_

// --- the interfaces ---------------------------------------------------------

struct D3D9VkExtImageDesc;   // opaque: only referenced by a method we never call

// ID3D9VkExtInterface, THE THIRD TRANSCRIPTION, moved here 2026-09-09 (audit
// F162). It lived inline inside HdrUnlockFormats, away from this file's whole
// point, which is that every hand-copied DXVK interface sits in one place under
// one instruction to check it against d3d9_interfaces.h. It matched when it was
// checked; the risk was that the next person to check these would not know it
// existed. ONE EXCEPTION (the final audit's critic, 2026-09-30): hdr_encode.h's HdrExtSwapchain hand-copies
// ID3D9VkExtSwapchain beside its only user; its slot order was checked against refs/dxvk-3.1 d3d9_interfaces.h:273-286.
//
// ONE METHOD, and one is all that is used, but that is also all there is: this
// interface has exactly UnlockAdditionalFormats after IUnknown in
// d3d9_interfaces.h, so there is no later slot to keep correct. It is queried
// from the IDirect3D9 object and must be called BEFORE CreateDevice, because
// the device caches the flag when it is constructed. That ordering is not
// stylistic and the reason is written out at the call site.
MIDL_INTERFACE("65b55086-e3e3-4c3e-b3a0-86815cce2c4c")
ID3D9VkExtInterfaceMin : public IUnknown {
    virtual void STDMETHODCALLTYPE UnlockAdditionalFormats() = 0;
};

MIDL_INTERFACE("d56344f5-8d35-46fd-806d-94c351b472c1")
ID3D9VkInteropTexture : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
        VkImage*           pHandle,
        VkImageLayout*     pLayout,
        VkImageCreateInfo* pInfo) = 0;
};

MIDL_INTERFACE("2eaa4b89-0107-4bdb-87f7-0f541c493ce0")
ID3D9VkInteropDevice : public IUnknown {
    virtual void STDMETHODCALLTYPE GetVulkanHandles(
        VkInstance* pInstance, VkPhysicalDevice* pPhysDev, VkDevice* pDevice) = 0;
    virtual void STDMETHODCALLTYPE GetSubmissionQueue(
        VkQueue* pQueue, uint32_t* pQueueIndex, uint32_t* pQueueFamilyIndex) = 0;
    virtual void STDMETHODCALLTYPE TransitionTextureLayout(
        ID3D9VkInteropTexture*         pTexture,
        const VkImageSubresourceRange* pSubresources,
        VkImageLayout                  OldLayout,
        VkImageLayout                  NewLayout) = 0;
    virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
    // CALLED ON EVERY INJECTION AND PRESENT MARKER: the lock is the transport's invariant (no D3D9 call between the two).
    virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
    virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
    // Kept so the vtable slots stay correct. Not called: LockDevice, UnlockDevice, WaitForResource, CreateImage.
    virtual void STDMETHODCALLTYPE LockDevice() = 0;
    virtual void STDMETHODCALLTYPE UnlockDevice() = 0;
    virtual bool STDMETHODCALLTYPE WaitForResource(
        IDirect3DResource9* pResource, DWORD MapFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateImage(
        const D3D9VkExtImageDesc* desc, IDirect3DResource9** ppResult) = 0;
};
