/**
 * @ Author: Amélie Heinrich @ Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#pragma once

#include <agfx/agfx.h>

/**
 * @file agfx_native.h
 * @brief Escape hatch: unwraps AGFX objects into the backend objects they are built on, so
 *        third-party libraries that speak a native API directly (DLSS, FSR, XeSS, MetalFX, Optick,
 *        RenderDoc, PIX, a custom compute pass) can be plugged into an AGFX renderer.
 *
 * Including this header does nothing on its own. Define the backend you want *before* including it:
 *
 * @code
 *   #define AGFX_EXPOSE_D3D12   // pulls in <d3d12.h> and <dxgi1_6.h>
 *   #define AGFX_EXPOSE_METAL   // pulls in <Metal/Metal.h> and <QuartzCore/CAMetalLayer.h>
 *   #define AGFX_EXPOSE_VULKAN  // pulls in <vulkan/vulkan.h>
 *   #include <agfx/agfx_native.h>
 * @endcode
 *
 * The defines gate the *declarations*, not the implementations: the accessors are always compiled
 * into the library for whichever backend was built. So the define costs you nothing but the native
 * headers, and getting it wrong is a compile error rather than a link-time surprise.
 *
 * @note Ownership is never transferred. Every accessor returns a borrowed reference that AGFX still
 *       owns and destroys; it stays valid exactly as long as the AGFX object wrapping it does. Do
 *       not Release() a D3D12 object, and do not assume a returned object outlives an
 *       agfxSwapChainResize or an agfx*Destroy. If you need it longer, retain it yourself.
 *
 * @note AGFX does not observe what you do behind its back. It cannot know that a native library
 *       transitioned a resource, opened an encoder, or wrote a texture. Two rules follow, and both
 *       are easier to break than they look:
 *
 *       1. **Resource states are yours to restore.** Transition resources into whatever the native
 *          library documents *using AGFX barriers* before handing them over, and leave them in a
 *          state AGFX's next use expects. See `agfx-synchronization`.
 *       2. **Hand over a command buffer only outside a pass.** Native libraries that record work
 *          (every upscaler does) open their own encoder/command-list scope. On Metal, calling one
 *          while an agfxRenderPass or agfxComputePass is still open is invalid -- Metal permits
 *          exactly one open encoder per command buffer. End the pass first.
 *
 * @note Resources the native library creates for itself are invisible to AGFX. On Metal that
 *       matters concretely: they are not in AGFX's residency set, and a non-resident resource is a
 *       GPU fault. Add them yourself via agfxNativeGetMTLResidencySet, then call
 *       agfxDeviceMakeResourcesResident. The reverse direction -- adopting a native texture *into*
 *       an agfxTexture so the rest of AGFX can render to it -- is deliberately not offered here;
 *       it needs real ownership and view/bindless plumbing, not a cast.
 */

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------
// D3D12
// ---------------------------------------------------------------------------------------------

#if defined(AGFX_EXPOSE_D3D12)

// Define AGFX_NATIVE_NO_INCLUDES if you already include the native headers yourself -- notably if
// you use the Agility SDK, whose d3d12.h must be included instead of the system one, not after it.
#if !defined(AGFX_NATIVE_NO_INCLUDES)
#include <d3d12.h>
#include <dxgi1_6.h>
#endif

/// @brief Returns the ID3D12Device7 backing the device. The root object every native library wants.
ID3D12Device7* agfxNativeGetD3D12Device(agfxDevice* device);

/// @brief Returns the IDXGIAdapter4 the device was created on, for adapter/vendor capability queries.
IDXGIAdapter4* agfxNativeGetDXGIAdapter(agfxDevice* device);

/// @brief Returns the ID3D12CommandQueue backing the queue, for libraries that submit their own work.
ID3D12CommandQueue* agfxNativeGetD3D12CommandQueue(agfxCommandQueue* queue);

/// @brief Returns the command list the command buffer records into.
/// @note Only meaningful between agfxCommandBufferBegin and agfxCommandBufferEnd. Recording into it
///       while an AGFX render/compute pass is open works on D3D12 but not on Metal -- end the pass
///       first if the code is meant to be cross-platform.
ID3D12GraphicsCommandList10* agfxNativeGetD3D12CommandList(agfxCommandBuffer* commandBuffer);

/// @brief Returns the ID3D12Resource backing a texture.
ID3D12Resource* agfxNativeGetD3D12TextureResource(agfxTexture* texture);

/// @brief Returns the ID3D12Resource backing a buffer.
ID3D12Resource* agfxNativeGetD3D12BufferResource(agfxBuffer* buffer);

/// @brief Returns the ID3D12Heap backing a placement heap, for placing native resources alongside
///        AGFX ones. You are responsible for offsets not colliding with AGFX's -- see agfx-resources.
ID3D12Heap* agfxNativeGetD3D12Heap(agfxHeap* heap);

/// @brief Returns the ID3D12Fence backing a fence, for interop with a library that signals or waits itself.
/// @note AGFX tracks its own monotonic value for this fence. Signal your own values through
///       agfxCommandQueueSignal rather than the raw fence, or the two will fight over the sequence.
ID3D12Fence* agfxNativeGetD3D12Fence(agfxFence* fence);

/// @brief Returns the IDXGISwapChain4 backing the swap chain.
/// @note Invalidated by agfxSwapChainResize and by an HDR toggle, both of which recreate it. Re-query
///       after either; never cache across frames.
IDXGISwapChain4* agfxNativeGetDXGISwapChain(agfxSwapChain* swapChain);

#endif // AGFX_EXPOSE_D3D12

// ---------------------------------------------------------------------------------------------
// Metal
// ---------------------------------------------------------------------------------------------

#if defined(AGFX_EXPOSE_METAL)

#ifdef __OBJC__
#if !defined(AGFX_NATIVE_NO_INCLUDES)
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif
/// @brief Expands to a real Objective-C type in an .m/.mm translation unit.
#define AGFX_NATIVE_MTL(Protocol) id<Protocol>
#define AGFX_NATIVE_MTL_CLASS(Class) Class*
#else
/// @brief Expands to void* in a plain C/C++ translation unit, so this header stays includable there.
///        Same pointer ABI -- an ObjC++ .mm can consume the void* and cast it back.
#define AGFX_NATIVE_MTL(Protocol) void*
#define AGFX_NATIVE_MTL_CLASS(Class) void*
#endif

/// @brief Returns the MTLDevice backing the device. The root object every native library wants.
AGFX_NATIVE_MTL(MTLDevice) agfxNativeGetMTLDevice(agfxDevice* device);

/// @brief Returns the device-wide MTLResidencySet AGFX puts every resource it allocates into.
/// @note This is the one accessor a MetalFX integration genuinely cannot skip. Textures a native
///       library allocates itself are not resident as far as AGFX is concerned, and using a
///       non-resident resource is a GPU fault, not a warning. Add them with
///       [set addAllocation:...], then call agfxDeviceMakeResourcesResident to commit.
AGFX_NATIVE_MTL(MTLResidencySet) agfxNativeGetMTLResidencySet(agfxDevice* device);

/// @brief Returns the MTL4CommandQueue backing the queue.
AGFX_NATIVE_MTL(MTL4CommandQueue) agfxNativeGetMTL4CommandQueue(agfxCommandQueue* queue);

/// @brief Returns the MTL4CommandBuffer the command buffer records into.
/// @note Only meaningful between agfxCommandBufferBegin and agfxCommandBufferEnd, and only while no
///       AGFX pass is open -- Metal allows one open encoder per command buffer, so a native library
///       that opens its own (MetalFX does) will fail validation otherwise. This is the Metal 4
///       command buffer type: MetalFX's MTL4FX* interfaces take it, the Metal 3 MTLFX* ones do not.
AGFX_NATIVE_MTL(MTL4CommandBuffer) agfxNativeGetMTL4CommandBuffer(agfxCommandBuffer* commandBuffer);

/// @brief Returns the MTLTexture backing a texture.
/// @note For a texture from agfxSwapChainAcquireNextTexture this is the current drawable's texture,
///       which changes every frame -- re-query it each acquire rather than caching it.
AGFX_NATIVE_MTL(MTLTexture) agfxNativeGetMTLTexture(agfxTexture* texture);

/// @brief Returns the MTLBuffer backing a buffer.
AGFX_NATIVE_MTL(MTLBuffer) agfxNativeGetMTLBuffer(agfxBuffer* buffer);

/// @brief Returns the MTLHeap backing a placement heap, for placing native resources alongside AGFX
///        ones. The heap is already in the residency set, so anything you place in it is resident
///        for free -- but you own the offsets not colliding with AGFX's. See agfx-resources.
AGFX_NATIVE_MTL(MTLHeap) agfxNativeGetMTLHeap(agfxHeap* heap);

/// @brief Returns the MTLSharedEvent backing a fence.
/// @note As on D3D12, AGFX owns the value sequence -- prefer agfxCommandQueueSignal over signaling
///       the raw event, or the two will fight over the sequence.
AGFX_NATIVE_MTL(MTLSharedEvent) agfxNativeGetMTLSharedEvent(agfxFence* fence);

/// @brief Returns the CAMetalLayer the swap chain presents through, for EDR/HDR headroom queries,
///        colorspace overrides, or a CAMetalDisplayLink.
AGFX_NATIVE_MTL_CLASS(CAMetalLayer) agfxNativeGetCAMetalLayer(agfxSwapChain* swapChain);

#endif // AGFX_EXPOSE_METAL

// ---------------------------------------------------------------------------------------------
// Vulkan
// ---------------------------------------------------------------------------------------------

#if defined(AGFX_EXPOSE_VULKAN)

#if !defined(AGFX_NATIVE_NO_INCLUDES)
#include <vulkan/vulkan.h>
#endif

/// @brief Returns the VkInstance backing the device.
VkInstance agfxNativeGetVkInstance(agfxDevice* device);

/// @brief Returns the VkPhysicalDevice the device was created on, for adapter/vendor capability queries.
VkPhysicalDevice agfxNativeGetVkPhysicalDevice(agfxDevice* device);

/// @brief Returns the VkDevice backing the device. The root object every native library wants.
VkDevice agfxNativeGetVkDevice(agfxDevice* device);

/// @brief Returns the VkQueue backing the queue, for libraries that submit their own work.
VkQueue agfxNativeGetVkQueue(agfxCommandQueue* queue);

/// @brief Returns the VkImage backing a texture.
VkImage agfxNativeGetVkImage(agfxTexture* texture);

/// @brief Returns the VkBuffer backing a buffer.
VkBuffer agfxNativeGetVkBuffer(agfxBuffer* buffer);

/// @brief Returns the VkDeviceMemory backing a placement heap, for placing native resources alongside
///        AGFX ones. You are responsible for offsets not colliding with AGFX's -- see agfx-resources.
VkDeviceMemory agfxNativeGetVkHeapMemory(agfxHeap* heap);

/// @brief Returns the VkSemaphore (timeline) backing a fence, for interop with a library that
///        signals or waits itself.
/// @note AGFX tracks its own monotonic value for this timeline. Signal your own values through
///       agfxCommandQueueSignal rather than the raw semaphore, or the two will fight over the sequence.
VkSemaphore agfxNativeGetVkSemaphore(agfxFence* fence);

#endif // AGFX_EXPOSE_VULKAN

#ifdef __cplusplus
}
#endif
