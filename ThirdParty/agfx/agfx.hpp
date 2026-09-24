/**
 * @ Author: Amélie Heinrich @ Amélie Heinrich
 * @ Create Time: 2026-07-20 00:00:00
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

// agfx.hpp: Header-only, move-only RAII wrapper over the agfx C API. Every agfx*
// object is destroyed automatically when its wrapper goes out of scope. Also provides
// scoped enum mirrors of the C enums and builder-style create-info structs with
// chainable setters. Requires C++17.

#pragma once

#include <agfx/agfx.h>

#include <cfloat>
#include <cstring>
#include <initializer_list>

namespace agfx
{
    // ----------------------------------------------------------------------------------
    // Scoped enum mirrors of the C enums. Enumerator values are pinned to their C
    // counterparts, so a static_cast between the two is always valid.
    // ----------------------------------------------------------------------------------

    /// @brief Passed as the mip/layer of a barrier to target all mips/layers of a texture.
    inline constexpr uint32_t AllMips = static_cast<uint32_t>(AGFX_SUBRESOURCE_ALL_MIPS);
    inline constexpr uint32_t AllLayers = static_cast<uint32_t>(AGFX_SUBRESOURCE_ALL_LAYERS);

    enum class LogSeverity
    {
        Info = AGFX_LOG_SEVERITY_INFO,
        Warning = AGFX_LOG_SEVERITY_WARNING,
        Error = AGFX_LOG_SEVERITY_ERROR,
    };

    enum class CommandQueueType
    {
        Graphics = AGFX_COMMAND_QUEUE_TYPE_GRAPHICS,
        Compute = AGFX_COMMAND_QUEUE_TYPE_COMPUTE,
        Transfer = AGFX_COMMAND_QUEUE_TYPE_TRANSFER,
    };

    enum class ResourceState
    {
        Common = AGFX_RESOURCE_STATE_COMMON,
        VertexAndConstantBuffer = AGFX_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        IndexBuffer = AGFX_RESOURCE_STATE_INDEX_BUFFER,
        RenderTarget = AGFX_RESOURCE_STATE_RENDER_TARGET,
        UnorderedAccess = AGFX_RESOURCE_STATE_UNORDERED_ACCESS,
        DepthWrite = AGFX_RESOURCE_STATE_DEPTH_WRITE,
        DepthRead = AGFX_RESOURCE_STATE_DEPTH_READ,
        NonPixelShaderResource = AGFX_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        PixelShaderResource = AGFX_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        IndirectArgument = AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT,
        CopyDest = AGFX_RESOURCE_STATE_COPY_DEST,
        CopySource = AGFX_RESOURCE_STATE_COPY_SOURCE,
        RaytracingAccelerationStructure = AGFX_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        GenericRead = AGFX_RESOURCE_STATE_GENERIC_READ,
        AllShaderResource = AGFX_RESOURCE_STATE_ALL_SHADER_RESOURCE,
        Present = AGFX_RESOURCE_STATE_PRESENT,
    };

    enum class TextureFormat
    {
        Unknown = AGFX_TEXTURE_FORMAT_UNKNOWN,

        R8Unorm = AGFX_TEXTURE_FORMAT_R8_UNORM,
        RG8Unorm = AGFX_TEXTURE_FORMAT_RG8_UNORM,
        RGBA8Unorm = AGFX_TEXTURE_FORMAT_RGBA8_UNORM,
        BGRA8Unorm = AGFX_TEXTURE_FORMAT_BGRA8_UNORM,
        RGBA8UnormSRGB = AGFX_TEXTURE_FORMAT_RGBA8_UNORM_SRGB,
        BGRA8UnormSRGB = AGFX_TEXTURE_FORMAT_BGRA8_UNORM_SRGB,

        R16Unorm = AGFX_TEXTURE_FORMAT_R16_UNORM,
        RG16Unorm = AGFX_TEXTURE_FORMAT_RG16_UNORM,
        RGBA16Unorm = AGFX_TEXTURE_FORMAT_RGBA16_UNORM,

        R16F = AGFX_TEXTURE_FORMAT_R16F,
        RG16F = AGFX_TEXTURE_FORMAT_RG16F,
        RGBA16F = AGFX_TEXTURE_FORMAT_RGBA16F,

        R32F = AGFX_TEXTURE_FORMAT_R32F,
        RG32F = AGFX_TEXTURE_FORMAT_RG32F,
        RGBA32F = AGFX_TEXTURE_FORMAT_RGBA32F,

        Depth32F = AGFX_TEXTURE_FORMAT_DEPTH32F,

        BC1Unorm = AGFX_TEXTURE_FORMAT_BC1_UNORM,
        BC1UnormSRGB = AGFX_TEXTURE_FORMAT_BC1_UNORM_SRGB,
        BC3Unorm = AGFX_TEXTURE_FORMAT_BC3_UNORM,
        BC3UnormSRGB = AGFX_TEXTURE_FORMAT_BC3_UNORM_SRGB,
        BC4Unorm = AGFX_TEXTURE_FORMAT_BC4_UNORM,
        BC5Unorm = AGFX_TEXTURE_FORMAT_BC5_UNORM,
        BC6HUFloat = AGFX_TEXTURE_FORMAT_BC6H_UFLOAT,
        BC7Unorm = AGFX_TEXTURE_FORMAT_BC7_UNORM,
        BC7UnormSRGB = AGFX_TEXTURE_FORMAT_BC7_UNORM_SRGB,

        ASTC4x4Unorm = AGFX_TEXTURE_FORMAT_ASTC_4X4_UNORM,
        ASTC4x4UnormSRGB = AGFX_TEXTURE_FORMAT_ASTC_4X4_UNORM_SRGB,
        ASTC8x8Unorm = AGFX_TEXTURE_FORMAT_ASTC_8X8_UNORM,
        ASTC8x8UnormSRGB = AGFX_TEXTURE_FORMAT_ASTC_8X8_UNORM_SRGB,
    };

    enum class TextureType
    {
        Texture1D = AGFX_TEXTURE_TYPE_1D,
        Texture2D = AGFX_TEXTURE_TYPE_2D,
        Texture2DArray = AGFX_TEXTURE_TYPE_2D_ARRAY,
        Texture3D = AGFX_TEXTURE_TYPE_3D,
        Cube = AGFX_TEXTURE_TYPE_CUBE,
    };

    /// @brief Bit flags; combine with operator|.
    enum class TextureUsage : uint32_t
    {
        Sampled = AGFX_TEXTURE_USAGE_SAMPLED,
        Storage = AGFX_TEXTURE_USAGE_STORAGE,
        ColorAttachment = AGFX_TEXTURE_USAGE_COLOR_ATTACHMENT,
        DepthStencilAttachment = AGFX_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT,
    };

    inline constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) { return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
    inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b) { a = a | b; return a; }
    inline constexpr bool HasFlag(TextureUsage value, TextureUsage flag) { return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0; }

    /// @brief Bit flags; combine with operator|.
    enum class BufferUsage : uint32_t
    {
        Index = AGFX_BUFFER_USAGE_INDEX,
        Constant = AGFX_BUFFER_USAGE_CONSTANT,
        ShaderRead = AGFX_BUFFER_USAGE_SHADER_READ,
        ShaderWrite = AGFX_BUFFER_USAGE_SHADER_WRITE,
    };

    inline constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
    inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b) { a = a | b; return a; }
    inline constexpr bool HasFlag(BufferUsage value, BufferUsage flag) { return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0; }

    enum class BufferMemoryType
    {
        None = AGFX_BUFFER_MEMORY_TYPE_NONE,
        GPUOnly = AGFX_BUFFER_MEMORY_TYPE_GPU_ONLY,
        CPUToGPU = AGFX_BUFFER_MEMORY_TYPE_CPU_TO_GPU,
        GPUToCPU = AGFX_BUFFER_MEMORY_TYPE_GPU_TO_CPU,
        Default = AGFX_BUFFER_MEMORY_TYPE_DEFAULT,
        Upload = AGFX_BUFFER_MEMORY_TYPE_UPLOAD,
        Readback = AGFX_BUFFER_MEMORY_TYPE_READBACK,
    };

    enum class BufferViewType
    {
        Raw = AGFX_BUFFER_VIEW_TYPE_RAW,
        Structured = AGFX_BUFFER_VIEW_TYPE_STRUCTURED,
        Constant = AGFX_BUFFER_VIEW_TYPE_CONSTANT,
    };

    enum class SamplerFilter
    {
        Nearest = AGFX_SAMPLER_FILTER_NEAREST,
        Linear = AGFX_SAMPLER_FILTER_LINEAR,
    };

    enum class AddressMode
    {
        Repeat = AGFX_SAMPLER_ADDRESS_MODE_REPEAT,
        MirroredRepeat = AGFX_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        ClampToEdge = AGFX_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        ClampToBorder = AGFX_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    };

    enum class ComparisonFunction
    {
        Never = AGFX_COMPARISON_FUNCTION_NEVER,
        Less = AGFX_COMPARISON_FUNCTION_LESS,
        Equal = AGFX_COMPARISON_FUNCTION_EQUAL,
        LessEqual = AGFX_COMPARISON_FUNCTION_LESS_EQUAL,
        Greater = AGFX_COMPARISON_FUNCTION_GREATER,
        NotEqual = AGFX_COMPARISON_FUNCTION_NOT_EQUAL,
        GreaterEqual = AGFX_COMPARISON_FUNCTION_GREATER_EQUAL,
        Always = AGFX_COMPARISON_FUNCTION_ALWAYS,
    };

    enum class LoadOp
    {
        Load = AGFX_LOAD_OPERATION_LOAD,
        Clear = AGFX_LOAD_OPERATION_CLEAR,
        DontCare = AGFX_LOAD_OPERATION_DONT_CARE,
    };

    enum class StoreOp
    {
        Store = AGFX_STORE_OPERATION_STORE,
        DontCare = AGFX_STORE_OPERATION_DONT_CARE,
    };

    enum class ShaderModuleType
    {
        Vertex = AGFX_SHADER_MODULE_TYPE_VERTEX,
        Fragment = AGFX_SHADER_MODULE_TYPE_FRAGMENT,
        Compute = AGFX_SHADER_MODULE_TYPE_COMPUTE,
        Task = AGFX_SHADER_MODULE_TYPE_TASK,
        Mesh = AGFX_SHADER_MODULE_TYPE_MESH,
    };

    enum class Topology
    {
        Triangles = AGFX_TOPOLOGY_TRIANGLES,
        Lines = AGFX_TOPOLOGY_LINES,
        Points = AGFX_TOPOLOGY_POINTS,
    };

    enum class CullMode
    {
        None = AGFX_CULL_MODE_NONE,
        Front = AGFX_CULL_MODE_FRONT,
        Back = AGFX_CULL_MODE_BACK,
    };

    enum class FrontFace
    {
        Clockwise = AGFX_FRONT_FACE_CLOCKWISE,
        CounterClockwise = AGFX_FRONT_FACE_COUNTER_CLOCKWISE,
    };

    enum class FillMode
    {
        Solid = AGFX_FILL_MODE_SOLID,
        Wireframe = AGFX_FILL_MODE_WIREFRAME,
    };

    enum class BlendFactor
    {
        Zero = AGFX_BLEND_FACTOR_ZERO,
        One = AGFX_BLEND_FACTOR_ONE,
        SrcColor = AGFX_BLEND_FACTOR_SRC_COLOR,
        OneMinusSrcColor = AGFX_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        DstColor = AGFX_BLEND_FACTOR_DST_COLOR,
        OneMinusDstColor = AGFX_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
        SrcAlpha = AGFX_BLEND_FACTOR_SRC_ALPHA,
        OneMinusSrcAlpha = AGFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        DstAlpha = AGFX_BLEND_FACTOR_DST_ALPHA,
        OneMinusDstAlpha = AGFX_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    };

    enum class BlendOp
    {
        Add = AGFX_BLEND_OPERATION_ADD,
        Subtract = AGFX_BLEND_OPERATION_SUBTRACT,
        ReverseSubtract = AGFX_BLEND_OPERATION_REVERSE_SUBTRACT,
        Min = AGFX_BLEND_OPERATION_MIN,
        Max = AGFX_BLEND_OPERATION_MAX,
    };

    enum class AccelerationStructureType
    {
        TopLevel = AGFX_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        BottomLevel = AGFX_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
    };

    enum class AccelerationStructureGeometryType
    {
        Triangles = AGFX_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_TRIANGLES,
        AABBs = AGFX_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_AABBS,
    };

    enum class IndirectBundleType
    {
        Draw = AGFX_INDIRECT_BUNDLE_TYPE_DRAW,
        DrawIndexed = AGFX_INDIRECT_BUNDLE_TYPE_DRAW_INDEXED,
        DrawMesh = AGFX_INDIRECT_BUNDLE_TYPE_DRAW_MESH,
        Dispatch = AGFX_INDIRECT_BUNDLE_TYPE_DISPATCH,
    };

    // ----------------------------------------------------------------------------------
    // Create-info builders. Each inherits its C struct, so it passes directly anywhere a
    // const agfxXxxCreateInfo& is expected. Default-constructed state is zero-initialized
    // plus the sensible defaults noted per struct, e.g.:
    //
    //   agfx::Texture texture = device.CreateTexture(agfx::TextureCreateInfo()
    //       .SetFormat(agfx::TextureFormat::RGBA8Unorm)
    //       .SetUsage(agfx::TextureUsage::Sampled | agfx::TextureUsage::Storage)
    //       .SetSize(1280, 720));
    // ----------------------------------------------------------------------------------

    struct DeviceCreateInfo : agfxDeviceCreateInfo
    {
        DeviceCreateInfo() : agfxDeviceCreateInfo{} {}

        DeviceCreateInfo& SetAllocator(agfxAllocate allocateFn, agfxFree freeFn) { allocate = allocateFn; free = freeFn; return *this; }
        DeviceCreateInfo& SetTempAllocator(agfxAllocate allocateFn, agfxFree freeFn) { tempAllocate = allocateFn; tempFree = freeFn; return *this; }
        DeviceCreateInfo& SetUserData(void* value) { userData = value; return *this; }
        DeviceCreateInfo& SetValidation(bool enable) { enableValidation = enable ? 1 : 0; return *this; }
        DeviceCreateInfo& SetLogFunction(agfxLogFunction fn) { logFunction = fn; return *this; }
    };

    struct QueryPoolCreateInfo : agfxQueryPoolCreateInfo
    {
        QueryPoolCreateInfo() : agfxQueryPoolCreateInfo{} {}

        QueryPoolCreateInfo& SetCount(uint32_t value) { count = value; return *this; }
    };

    /// @brief Defaults to a 1x1x1 region at the origin.
    struct TextureRegion : agfxTextureRegion
    {
        TextureRegion() : agfxTextureRegion{} { width = 1; height = 1; depth = 1; }

        TextureRegion& SetOffset(uint32_t offsetX, uint32_t offsetY, uint32_t offsetZ = 0) { x = offsetX; y = offsetY; z = offsetZ; return *this; }
        TextureRegion& SetSize(uint32_t w, uint32_t h, uint32_t d = 1) { width = w; height = h; depth = d; return *this; }
    };

    /// @brief Defaults: 2D, 1x1x1, 1 mip, committed (no heap).
    struct TextureCreateInfo : agfxTextureCreateInfo
    {
        TextureCreateInfo() : agfxTextureCreateInfo{}
        {
            type = AGFX_TEXTURE_TYPE_2D;
            width = 1;
            height = 1;
            depthOrArrayLayers = 1;
            mipLevels = 1;
        }

        TextureCreateInfo& SetType(TextureType value) { type = static_cast<agfxTextureType>(value); return *this; }
        TextureCreateInfo& SetFormat(TextureFormat value) { format = static_cast<agfxTextureFormat>(value); return *this; }
        TextureCreateInfo& SetUsage(TextureUsage flags) { usage = static_cast<agfxTextureUsage>(flags); return *this; }
        TextureCreateInfo& SetSize(uint32_t w, uint32_t h, uint32_t depthOrLayers = 1) { width = w; height = h; depthOrArrayLayers = depthOrLayers; return *this; }
        TextureCreateInfo& SetMipLevels(uint32_t value) { mipLevels = value; return *this; }
        /// @brief Places the texture in a heap at the given offset; see agfxTextureCreateInfo::heapOffset for alignment rules.
        TextureCreateInfo& SetHeap(agfxHeap* value, uint64_t offset = 0) { heap = value; heapOffset = offset; return *this; }

        TextureType GetType() const { return static_cast<TextureType>(type); }
        TextureFormat GetFormat() const { return static_cast<TextureFormat>(format); }
        TextureUsage GetUsage() const { return static_cast<TextureUsage>(usage); }
    };

    /// @brief Defaults: GPU-only memory, committed (no heap).
    struct BufferCreateInfo : agfxBufferCreateInfo
    {
        BufferCreateInfo() : agfxBufferCreateInfo{} { memoryType = AGFX_BUFFER_MEMORY_TYPE_DEFAULT; }

        BufferCreateInfo& SetSize(uint64_t value) { size = value; return *this; }
        BufferCreateInfo& SetStride(uint64_t value) { stride = value; return *this; }
        BufferCreateInfo& SetUsage(BufferUsage flags) { usage = static_cast<agfxBufferUsage>(flags); return *this; }
        BufferCreateInfo& SetMemoryType(BufferMemoryType value) { memoryType = static_cast<agfxBufferMemoryType>(value); return *this; }
        /// @brief Places the buffer in a heap at the given offset; see agfxBufferCreateInfo::heapOffset for alignment rules.
        BufferCreateInfo& SetHeap(agfxHeap* value, uint64_t offset = 0) { heap = value; heapOffset = offset; return *this; }

        BufferUsage GetUsage() const { return static_cast<BufferUsage>(usage); }
        BufferMemoryType GetMemoryType() const { return static_cast<BufferMemoryType>(memoryType); }
    };

    /// @brief Defaults to GPU-only memory (the only kind textures may be placed in).
    struct HeapCreateInfo : agfxHeapCreateInfo
    {
        HeapCreateInfo() : agfxHeapCreateInfo{} { memoryType = AGFX_BUFFER_MEMORY_TYPE_GPU_ONLY; }

        HeapCreateInfo& SetSize(uint64_t value) { size = value; return *this; }
        HeapCreateInfo& SetMemoryType(BufferMemoryType value) { memoryType = static_cast<agfxBufferMemoryType>(value); return *this; }
    };

    /// @brief Defaults: 2D read-only view covering 1 mip and 1 layer.
    struct TextureViewCreateInfo : agfxTextureViewCreateInfo
    {
        TextureViewCreateInfo() : agfxTextureViewCreateInfo{}
        {
            type = AGFX_TEXTURE_TYPE_2D;
            mipLevelCount = 1;
            arrayLayerCount = 1;
        }

        TextureViewCreateInfo& SetTexture(agfxTexture* value) { texture = value; return *this; }
        TextureViewCreateInfo& SetFormat(TextureFormat value) { format = static_cast<agfxTextureFormat>(value); return *this; }
        TextureViewCreateInfo& SetType(TextureType value) { type = static_cast<agfxTextureType>(value); return *this; }
        TextureViewCreateInfo& SetMipRange(uint32_t base, uint32_t count) { baseMipLevel = base; mipLevelCount = count; return *this; }
        TextureViewCreateInfo& SetArrayRange(uint32_t base, uint32_t count) { baseArrayLayer = base; arrayLayerCount = count; return *this; }
        TextureViewCreateInfo& SetWriteable(bool value) { writeable = value ? 1 : 0; return *this; }
    };

    /// @brief Defaults: nearest/repeat, maxAnisotropy 1, LOD range [0, FLT_MAX].
    struct SamplerCreateInfo : agfxSamplerCreateInfo
    {
        SamplerCreateInfo() : agfxSamplerCreateInfo{}
        {
            maxAnisotropy = 1.0f;
            maxLod = FLT_MAX;
        }

        SamplerCreateInfo& SetFilter(SamplerFilter value) { filter = static_cast<agfxSamplerFilter>(value); return *this; }
        /// @brief Sets the same address mode on U, V, and W.
        SamplerCreateInfo& SetAddressMode(AddressMode value)
        {
            addressModeU = static_cast<agfxSamplerAddressMode>(value);
            addressModeV = static_cast<agfxSamplerAddressMode>(value);
            addressModeW = static_cast<agfxSamplerAddressMode>(value);
            return *this;
        }
        SamplerCreateInfo& SetAddressModes(AddressMode u, AddressMode v, AddressMode w)
        {
            addressModeU = static_cast<agfxSamplerAddressMode>(u);
            addressModeV = static_cast<agfxSamplerAddressMode>(v);
            addressModeW = static_cast<agfxSamplerAddressMode>(w);
            return *this;
        }
        SamplerCreateInfo& SetMipLodBias(float value) { mipLodBias = value; return *this; }
        SamplerCreateInfo& SetMaxAnisotropy(float value) { maxAnisotropy = value; return *this; }
        SamplerCreateInfo& SetComparisonFunction(ComparisonFunction value) { comparisonFunction = static_cast<agfxComparisonFunction>(value); return *this; }
        SamplerCreateInfo& SetLodRange(float min, float max) { minLod = min; maxLod = max; return *this; }
        SamplerCreateInfo& SetLodBias(float value) { lodBias = value; return *this; }
    };

    struct BufferViewCreateInfo : agfxBufferViewCreateInfo
    {
        BufferViewCreateInfo() : agfxBufferViewCreateInfo{} {}

        BufferViewCreateInfo& SetBuffer(agfxBuffer* value) { buffer = value; return *this; }
        BufferViewCreateInfo& SetType(BufferViewType value) { type = static_cast<agfxBufferViewType>(value); return *this; }
        BufferViewCreateInfo& SetOffset(uint64_t value) { offset = value; return *this; }
        BufferViewCreateInfo& SetWriteable(bool value) { writeable = value ? 1 : 0; return *this; }
    };

    struct RenderTargetCreateInfo : agfxRenderTargetCreateInfo
    {
        RenderTargetCreateInfo() : agfxRenderTargetCreateInfo{} {}

        RenderTargetCreateInfo& SetTexture(agfxTexture* value) { texture = value; return *this; }
        /// @brief Leave unset (TextureFormat::Unknown) to inherit the texture's own format, e.g. for swap chain back buffers.
        RenderTargetCreateInfo& SetFormat(TextureFormat value) { format = static_cast<agfxTextureFormat>(value); return *this; }
        RenderTargetCreateInfo& SetMipLevel(uint32_t value) { mipLevel = value; return *this; }
        RenderTargetCreateInfo& SetArrayLayer(uint32_t value) { arrayLayer = value; return *this; }
        RenderTargetCreateInfo& SetIsDepth(bool value) { isDepth = value ? 1 : 0; return *this; }
    };

    /// @brief Defaults: load/store, clearDepth 1.0f (the conventional non-reversed-Z clear).
    struct RenderPassAttachment : agfxRenderPassAttachment
    {
        RenderPassAttachment() : agfxRenderPassAttachment{} { clearDepth = 1.0f; }

        RenderPassAttachment& SetRenderTarget(agfxRenderTarget* value) { renderTarget = value; return *this; }
        RenderPassAttachment& SetLoadOp(LoadOp value) { loadOp = static_cast<agfxLoadOp>(value); return *this; }
        RenderPassAttachment& SetStoreOp(StoreOp value) { storeOp = static_cast<agfxStoreOp>(value); return *this; }
        /// @brief Sets loadOp to Clear with the given clear color.
        RenderPassAttachment& Clear(float r, float g, float b, float a = 1.0f)
        {
            loadOp = AGFX_LOAD_OPERATION_CLEAR;
            clearColor[0] = r; clearColor[1] = g; clearColor[2] = b; clearColor[3] = a;
            return *this;
        }
        /// @brief Sets loadOp to Clear with the given depth clear value.
        RenderPassAttachment& ClearDepth(float depth = 1.0f)
        {
            loadOp = AGFX_LOAD_OPERATION_CLEAR;
            clearDepth = depth;
            return *this;
        }
    };

    struct RenderPassCreateInfo : agfxRenderPassCreateInfo
    {
        RenderPassCreateInfo() : agfxRenderPassCreateInfo{} {}

        RenderPassCreateInfo& SetName(const char* value) { name = value; return *this; }
        RenderPassCreateInfo& SetSize(uint32_t w, uint32_t h) { width = w; height = h; return *this; }

        /// @brief Appends a color attachment (up to 8; extras are ignored).
        RenderPassCreateInfo& AddColorAttachment(const agfxRenderPassAttachment& attachment)
        {
            if (colorAttachmentCount < 8) {
                colorAttachments[colorAttachmentCount++] = attachment;
            }
            return *this;
        }

        RenderPassCreateInfo& SetDepthAttachment(const agfxRenderPassAttachment& attachment)
        {
            depthAttachment = attachment;
            hasDepthAttachment = 1;
            return *this;
        }
    };

    /// @brief Defaults: 2 back buffers, vsync on.
    struct SwapChainCreateInfo : agfxSwapChainCreateInfo
    {
        SwapChainCreateInfo() : agfxSwapChainCreateInfo{}
        {
            imageCount = 2;
            vsync = 1;
        }

        SwapChainCreateInfo& SetQueue(agfxCommandQueue* value) { queue = value; return *this; }
        SwapChainCreateInfo& SetImageCount(uint32_t value) { imageCount = value; return *this; }
        SwapChainCreateInfo& SetSize(uint32_t w, uint32_t h) { width = w; height = h; return *this; }
        SwapChainCreateInfo& SetHDR(bool value) { isHDR = value ? 1 : 0; return *this; }
        SwapChainCreateInfo& SetVSync(bool value) { vsync = value ? 1 : 0; return *this; }
        /// @brief The native window/layer handle: an HWND on Windows, or a CAMetalLayer* on macOS.
        SwapChainCreateInfo& SetWindowHandle(void* value) { handle = value; return *this; }
    };

    struct ShaderModuleCreateInfo : agfxShaderModuleCreateInfo
    {
        ShaderModuleCreateInfo() : agfxShaderModuleCreateInfo{} { entryPoint = "main"; }

        ShaderModuleCreateInfo& SetCode(uint8_t* bytecode, uint64_t size) { code = bytecode; codeSize = size; return *this; }
        ShaderModuleCreateInfo& SetEntryPoint(const char* value) { entryPoint = value; return *this; }
        ShaderModuleCreateInfo& SetType(ShaderModuleType value) { type = static_cast<agfxShaderModuleType>(value); return *this; }
    };

    /// @brief Defaults: solid fill, no culling, clockwise front face, triangles, depth compare Less
    /// (depth test/write still off until enabled). Blend setters apply to the most recently added
    /// color attachment, so call them right after AddColorAttachment.
    struct RenderPipelineCreateInfo : agfxRenderPipelineCreateInfo
    {
        RenderPipelineCreateInfo() : agfxRenderPipelineCreateInfo{} { depthCompareOp = AGFX_COMPARISON_FUNCTION_LESS; }

        RenderPipelineCreateInfo& SetName(const char* value) { name = value; return *this; }
        RenderPipelineCreateInfo& SetSupportsIndirect(bool value) { supportsIndirect = value ? 1 : 0; return *this; }

        RenderPipelineCreateInfo& SetFillMode(FillMode value) { fillMode = static_cast<agfxFillMode>(value); return *this; }
        RenderPipelineCreateInfo& SetCullMode(CullMode value) { cullMode = static_cast<agfxCullMode>(value); return *this; }
        RenderPipelineCreateInfo& SetFrontFace(FrontFace value) { frontFace = static_cast<agfxFrontFace>(value); return *this; }
        RenderPipelineCreateInfo& SetTopology(Topology value) { topology = static_cast<agfxTopology>(value); return *this; }

        RenderPipelineCreateInfo& SetDepthState(bool testEnable, bool writeEnable, ComparisonFunction compareOp = ComparisonFunction::Less)
        {
            depthTestEnable = testEnable ? 1 : 0;
            depthWriteEnable = writeEnable ? 1 : 0;
            depthCompareOp = static_cast<agfxComparisonFunction>(compareOp);
            return *this;
        }
        RenderPipelineCreateInfo& SetDepthClamp(bool value) { depthClampEnable = value ? 1 : 0; return *this; }
        RenderPipelineCreateInfo& SetDepthFormat(TextureFormat value) { depthFormat = static_cast<agfxTextureFormat>(value); return *this; }

        /// @brief Appends a color attachment with blending disabled (up to 8; extras are ignored).
        RenderPipelineCreateInfo& AddColorAttachment(TextureFormat format)
        {
            if (colorAttachmentCount < 8) {
                colorFormats[colorAttachmentCount++] = static_cast<agfxTextureFormat>(format);
            }
            return *this;
        }

        /// @brief Enables blending on the last added color attachment with the given factors/ops.
        RenderPipelineCreateInfo& SetBlend(BlendFactor srcColor, BlendFactor dstColor, BlendOp colorOp,
                                           BlendFactor srcAlpha, BlendFactor dstAlpha, BlendOp alphaOp)
        {
            if (colorAttachmentCount > 0) {
                uint32_t i = colorAttachmentCount - 1;
                blendEnable[i] = 1;
                srcColorBlendFactor[i] = static_cast<agfxBlendFactor>(srcColor);
                dstColorBlendFactor[i] = static_cast<agfxBlendFactor>(dstColor);
                colorBlendOp[i] = static_cast<agfxBlendOperation>(colorOp);
                srcAlphaBlendFactor[i] = static_cast<agfxBlendFactor>(srcAlpha);
                dstAlphaBlendFactor[i] = static_cast<agfxBlendFactor>(dstAlpha);
                alphaBlendOp[i] = static_cast<agfxBlendOperation>(alphaOp);
            }
            return *this;
        }

        /// @brief Enables standard alpha blending (srcAlpha, 1-srcAlpha) on the last added color attachment.
        RenderPipelineCreateInfo& SetAlphaBlend()
        {
            return SetBlend(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::Add,
                            BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add);
        }

        RenderPipelineCreateInfo& SetVertexShader(agfxShaderModule* value) { vertexShader = value; return *this; }
        RenderPipelineCreateInfo& SetFragmentShader(agfxShaderModule* value) { fragmentShader = value; return *this; }
        RenderPipelineCreateInfo& SetTaskShader(agfxShaderModule* value, uint32_t groupSizeX, uint32_t groupSizeY = 1, uint32_t groupSizeZ = 1)
        {
            taskShader = value;
            taskGroupSizeX = groupSizeX;
            taskGroupSizeY = groupSizeY;
            taskGroupSizeZ = groupSizeZ;
            return *this;
        }
        RenderPipelineCreateInfo& SetMeshShader(agfxShaderModule* value, uint32_t groupSizeX, uint32_t groupSizeY = 1, uint32_t groupSizeZ = 1)
        {
            meshShader = value;
            meshGroupSizeX = groupSizeX;
            meshGroupSizeY = groupSizeY;
            meshGroupSizeZ = groupSizeZ;
            return *this;
        }

        RenderPipelineCreateInfo& SetCache(uint8_t* data, uint64_t size) { cache = data; cacheSize = size; return *this; }
    };

    struct ComputePipelineCreateInfo : agfxComputePipelineCreateInfo
    {
        ComputePipelineCreateInfo() : agfxComputePipelineCreateInfo{} {}

        ComputePipelineCreateInfo& SetName(const char* value) { name = value; return *this; }
        ComputePipelineCreateInfo& SetShader(agfxShaderModule* value, uint32_t sizeX, uint32_t sizeY = 1, uint32_t sizeZ = 1)
        {
            computeShader = value;
            groupSizeX = sizeX;
            groupSizeY = sizeY;
            groupSizeZ = sizeZ;
            return *this;
        }
        ComputePipelineCreateInfo& SetCache(uint8_t* data, uint64_t size) { cache = data; cacheSize = size; return *this; }
    };

    struct AccelerationStructureGeometry : agfxAccelerationStructureGeometry
    {
        AccelerationStructureGeometry() : agfxAccelerationStructureGeometry{} {}

        AccelerationStructureGeometry& SetOpaque(bool value) { opaque = value ? 1 : 0; return *this; }

        AccelerationStructureGeometry& SetTriangles(agfxBuffer* vertexBuffer, uint32_t vertexCount, agfxBuffer* indexBuffer, uint32_t indexCount,
                                                    uint32_t vertexOffset = 0, uint32_t indexOffset = 0)
        {
            type = AGFX_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_TRIANGLES;
            triangles.vertexBuffer = vertexBuffer;
            triangles.vertexOffset = vertexOffset;
            triangles.vertexCount = vertexCount;
            triangles.indexBuffer = indexBuffer;
            triangles.indexCount = indexCount;
            triangles.indexOffset = indexOffset;
            return *this;
        }

        AccelerationStructureGeometry& SetAABBs(agfxBuffer* aabbBuffer, uint32_t aabbCount, uint32_t aabbStride, uint32_t aabbOffset = 0)
        {
            type = AGFX_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_AABBS;
            aabbs.aabbBuffer = aabbBuffer;
            aabbs.aabbOffset = aabbOffset;
            aabbs.aabbCount = aabbCount;
            aabbs.aabbStride = aabbStride;
            return *this;
        }
    };

    /// @brief Defaults to the identity transform.
    struct AccelerationStructureInstance : agfxAccelerationStructureInstance
    {
        AccelerationStructureInstance() : agfxAccelerationStructureInstance{}
        {
            transform[0] = 1.0f;
            transform[5] = 1.0f;
            transform[10] = 1.0f;
        }

        AccelerationStructureInstance& SetBLAS(agfxAccelerationStructure* value) { blas = value; return *this; }
        /// @brief Copies a 3x4 row-major transform matrix (12 floats).
        AccelerationStructureInstance& SetTransform(const float* rowMajor3x4) { std::memcpy(transform, rowMajor3x4, sizeof(transform)); return *this; }
        AccelerationStructureInstance& SetUserID(uint32_t value) { userID = value; return *this; }
        AccelerationStructureInstance& SetOpaque(bool value) { opaque = value ? 1 : 0; return *this; }
    };

    struct AccelerationStructureCreateInfo : agfxAccelerationStructureCreateInfo
    {
        AccelerationStructureCreateInfo() : agfxAccelerationStructureCreateInfo{} {}

        /// @brief Makes this a bottom-level create info over the given geometries. The array must
        /// stay alive until agfxAccelerationStructureCreate is called.
        AccelerationStructureCreateInfo& SetBottomLevel(agfxAccelerationStructureGeometry* geometries, uint32_t geometryCount)
        {
            type = AGFX_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bottomLevel.geometries = geometries;
            bottomLevel.geometryCount = geometryCount;
            return *this;
        }

        /// @brief Makes this a top-level create info holding at most maxInstanceCount instances.
        AccelerationStructureCreateInfo& SetTopLevel(uint32_t maxInstanceCount)
        {
            type = AGFX_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            topLevel.maxInstanceCount = maxInstanceCount;
            return *this;
        }

        AccelerationStructureCreateInfo& SetAllowUpdate(bool value) { allowUpdate = value ? 1 : 0; return *this; }
        AccelerationStructureCreateInfo& SetName(const char* value) { name = value; return *this; }
    };

    struct IndirectBundleCreateInfo : agfxIndirectBundleCreateInfo
    {
        IndirectBundleCreateInfo() : agfxIndirectBundleCreateInfo{} { maxCountCount = 1; }

        IndirectBundleCreateInfo& SetType(IndirectBundleType value) { type = static_cast<agfxIndirectBundleType>(value); return *this; }
        IndirectBundleCreateInfo& SetMaxCommandCount(uint32_t value) { maxCommandCount = value; return *this; }
        IndirectBundleCreateInfo& SetMaxCountCount(uint32_t value) { maxCountCount = value; return *this; }
    };

    struct IndirectBundleExecuteInfo : agfxIndirectBundleExecuteInfo
    {
        IndirectBundleExecuteInfo() : agfxIndirectBundleExecuteInfo{} {}

        IndirectBundleExecuteInfo& SetCountIndex(uint32_t value) { countIndex = value; return *this; }
        IndirectBundleExecuteInfo& SetCommandRange(uint32_t offset, uint32_t maxCount) { commandOffset = offset; maxCommandCount = maxCount; return *this; }

        /// @brief Copies push-constant data (multiple of 4 bytes, clamped to 128 bytes).
        IndirectBundleExecuteInfo& SetPushConstants(const void* data, uint32_t size)
        {
            std::memcpy(pushConstants, data, size > sizeof(pushConstants) ? sizeof(pushConstants) : size);
            return *this;
        }
        template<typename T>
        IndirectBundleExecuteInfo& SetPushConstants(const T& data)
        {
            static_assert(sizeof(T) <= 128, "Push constants must not exceed 128 bytes");
            return SetPushConstants(&data, sizeof(T));
        }

        IndirectBundleExecuteInfo& SetRenderPipeline(agfxRenderPipeline* value) { renderPipeline = value; return *this; }
        IndirectBundleExecuteInfo& SetComputePipeline(agfxComputePipeline* value) { computePipeline = value; return *this; }
        IndirectBundleExecuteInfo& SetIndexBuffer(agfxBuffer* value) { indexBuffer = value; return *this; }
    };

    // Generic move-only owner for any agfx object destroyed via Destroy(device, handle).
    template<typename T, void(*Destroy)(agfxDevice*, T*)>
    class Handle
    {
    public:
        Handle() = default;
        Handle(agfxDevice* device, T* handle) : mDevice(device), mHandle(handle) {}
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept
            : mDevice(other.mDevice), mHandle(other.mHandle), mOwning(other.mOwning)
        {
            other.mDevice = nullptr;
            other.mHandle = nullptr;
            other.mOwning = true;
        }

        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) {
                Reset();
                mDevice = other.mDevice;
                mHandle = other.mHandle;
                mOwning = other.mOwning;
                other.mDevice = nullptr;
                other.mHandle = nullptr;
                other.mOwning = true;
            }
            return *this;
        }

        ~Handle() { Reset(); }

        /// @brief Wraps a handle this wrapper does not own, e.g. a swap chain's back buffer texture.
        /// Reset()/the destructor will not destroy it; the real owner is still responsible for that.
        static Handle Borrow(agfxDevice* device, T* handle)
        {
            Handle h(device, handle);
            h.mOwning = false;
            return h;
        }

        /// @brief Destroys the owned object now, if any (no-op for a Borrow()'d handle). Leaves the wrapper empty.
        void Reset()
        {
            if (mHandle && mOwning) {
                Destroy(mDevice, mHandle);
            }
            mHandle = nullptr;
            mDevice = nullptr;
            mOwning = true;
        }

        T* Get() const { return mHandle; }
        agfxDevice* GetDevice() const { return mDevice; }
        operator T*() const { return mHandle; }
        explicit operator bool() const { return mHandle != nullptr; }

        /// @brief Relinquishes ownership without destroying the object, e.g. when handing it to
        /// an API (such as ImGui_ImplAGFX_Init) that takes ownership and destroys it itself.
        /// @return The previously owned handle. The wrapper is left empty.
        T* Release()
        {
            T* handle = mHandle;
            mHandle = nullptr;
            mDevice = nullptr;
            mOwning = true;
            return handle;
        }

    protected:
        agfxDevice* mDevice = nullptr;
        T* mHandle = nullptr;
        bool mOwning = true;
    };

    class Fence : public Handle<agfxFence, agfxFenceDestroy>
    {
    public:
        using Handle::Handle;

        void Wait(uint64_t value, uint64_t timeoutMs = UINT64_MAX) { agfxFenceWait(mHandle, value, timeoutMs); }
        void Signal(uint64_t value) { agfxFenceSignal(mHandle, value); }
        uint64_t GetCompletedValue() { return agfxFenceGetCompletedValue(mHandle); }
    };

    class QueryPool : public Handle<agfxQueryPool, agfxQueryPoolDestroy>
    {
    public:
        using Handle::Handle;

        void Readback(uint32_t firstIndex, uint32_t count, uint64_t* outTimestampsNanoseconds)
        {
            agfxQueryPoolReadback(mDevice, mHandle, firstIndex, count, outTimestampsNanoseconds);
        }
    };

    /// @brief A memory heap that textures and buffers can be placed into at explicit offsets. Does
    /// not keep placed resources alive or extend their lifetime -- destroying a Heap while a
    /// resource is still placed in it is the caller's bug, not something this wrapper detects.
    class Heap : public Handle<agfxHeap, agfxHeapDestroy>
    {
    public:
        using Handle::Handle;
    };

    class Texture : public Handle<agfxTexture, agfxTextureDestroy>
    {
    public:
        using Handle::Handle;

        /// @brief Wraps a texture this object does not own, e.g. a swap chain's back buffer -- see
        /// SwapChain::AcquireNextTexture. Destroying the returned Texture will not destroy the underlying object.
        static Texture Borrow(agfxDevice* device, agfxTexture* handle)
        {
            Texture texture;
            texture.mDevice = device;
            texture.mHandle = handle;
            texture.mOwning = false;
            return texture;
        }

        TextureCreateInfo GetInfo() const
        {
            TextureCreateInfo info{};
            agfxTextureGetInfo(mHandle, &info);
            return info;
        }

        void ReplaceRegion(const agfxTextureRegion& region, uint32_t mipLevel, uint32_t layer, const void* data, uint32_t dataSize, uint32_t bytesPerRow, uint32_t bytesPerImage)
        {
            agfxTextureReplaceRegion(mDevice, mHandle, &region, mipLevel, layer, data, dataSize, bytesPerRow, bytesPerImage);
        }

        void SetName(const char* name) { agfxTextureSetName(mHandle, name); }
    };

    class Buffer : public Handle<agfxBuffer, agfxBufferDestroy>
    {
    public:
        using Handle::Handle;

        void* Map() { return agfxBufferMap(mHandle); }
        void Unmap() { agfxBufferUnmap(mHandle); }
        void SetName(const char* name) { agfxBufferSetName(mHandle, name); }

        BufferCreateInfo GetInfo() const
        {
            BufferCreateInfo info{};
            agfxBufferGetInfo(mHandle, &info);
            return info;
        }
    };

    /// @brief RAII guard around a mapped agfxBuffer; unmaps automatically when it goes out of scope.
    class MappedBuffer
    {
    public:
        explicit MappedBuffer(Buffer& buffer) : mBuffer(&buffer), mPtr(buffer.Map()) {}
        MappedBuffer(const MappedBuffer&) = delete;
        MappedBuffer& operator=(const MappedBuffer&) = delete;
        MappedBuffer(MappedBuffer&& other) noexcept : mBuffer(other.mBuffer), mPtr(other.mPtr) { other.mBuffer = nullptr; other.mPtr = nullptr; }

        ~MappedBuffer()
        {
            if (mBuffer) {
                mBuffer->Unmap();
            }
        }

        void* Get() const { return mPtr; }
        template<typename T> T* As() const { return static_cast<T*>(mPtr); }

    private:
        Buffer* mBuffer = nullptr;
        void* mPtr = nullptr;
    };

    class TextureView : public Handle<agfxTextureView, agfxTextureViewDestroy>
    {
    public:
        using Handle::Handle;

        uint64_t GetHandle() const { return agfxTextureViewGetHandle(mHandle); }
    };

    class Sampler : public Handle<agfxSampler, agfxSamplerDestroy>
    {
    public:
        using Handle::Handle;

        uint64_t GetHandle() const { return agfxSamplerGetHandle(mHandle); }
    };

    class BufferView : public Handle<agfxBufferView, agfxBufferViewDestroy>
    {
    public:
        using Handle::Handle;

        uint64_t GetHandle() const { return agfxBufferViewGetHandle(mHandle); }
    };

    class RenderTarget : public Handle<agfxRenderTarget, agfxRenderTargetDestroy>
    {
    public:
        using Handle::Handle;
    };

    /// @brief Owns an agfxAccelerationStructure (bottom- or top-level). Build/update/copy it
    /// through the corresponding ComputePass methods; sample it in shaders via GetHandle().
    class AccelerationStructure : public Handle<agfxAccelerationStructure, agfxAccelerationStructureDestroy>
    {
    public:
        using Handle::Handle;

        agfxAccelerationStructureSizes GetSizes() const
        {
            agfxAccelerationStructureSizes sizes{};
            agfxAccelerationStructureGetSizes(mDevice, mHandle, &sizes);
            return sizes;
        }

        /// @brief Bindless descriptor handle; write this into a push constant to trace against it.
        uint64_t GetHandle() const { return agfxAccelerationStructureGetHandle(mHandle); }

        /// @brief Appends instances to a top-level acceleration structure (call before building it).
        void AddInstances(const agfxAccelerationStructureInstance* instances, uint32_t instanceCount)
        {
            agfxAccelerationStructureAddInstances(mHandle, instances, instanceCount);
        }

        /// @brief Clears all previously-added instances from a top-level acceleration structure.
        void ResetInstances() { agfxAccelerationStructureResetInstances(mHandle); }
    };

    /// @brief Owns an agfxIndirectBundle (GPU-driven indirect draw/dispatch commands buffer + count
    /// buffer). Fill it from a compute shader via the AGFXIndirectDraw*Bundle HLSL helpers, then
    /// replay it with RenderPass::ExecuteIndirectBundle or ComputePass::ExecuteIndirectBundle.
    class IndirectBundle : public Handle<agfxIndirectBundle, agfxIndirectBundleDestroy>
    {
    public:
        using Handle::Handle;

        /// @brief Bindless handle to pass into the AGFXIndirectDraw*Bundle::Create HLSL helpers.
        uint64_t GetHandle() const { return agfxIndirectBundleGetHandle(mHandle); }

        /// @brief Non-owning access to the underlying commands/count buffers, e.g. for barriers.
        /// The bundle retains ownership; do not wrap these in an owning Buffer.
        agfxBuffer* CommandsBuffer() const { return agfxIndirectBundleGetCommandsBuffer(mHandle); }
        agfxBuffer* CountBuffer() const { return agfxIndirectBundleGetCountBuffer(mHandle); }
    };

    class ShaderModule : public Handle<agfxShaderModule, agfxShaderModuleDestroy>
    {
    public:
        using Handle::Handle;
    };

    class RenderPipeline : public Handle<agfxRenderPipeline, agfxRenderPipelineDestroy>
    {
    public:
        using Handle::Handle;
    };

    class ComputePipeline : public Handle<agfxComputePipeline, agfxComputePipelineDestroy>
    {
    public:
        using Handle::Handle;
    };

    /// @brief RAII scope guard around an agfxComputePass. Ends the pass automatically when it goes out of scope.
    class ComputePass
    {
    public:
        ComputePass() = default;
        explicit ComputePass(agfxComputePass* pass) : mPass(pass) {}
        ComputePass(const ComputePass&) = delete;
        ComputePass& operator=(const ComputePass&) = delete;
        ComputePass(ComputePass&& other) noexcept : mPass(other.mPass) { other.mPass = nullptr; }
        ComputePass& operator=(ComputePass&& other) noexcept
        {
            if (this != &other) { End(); mPass = other.mPass; other.mPass = nullptr; }
            return *this;
        }

        ~ComputePass() { End(); }

        void End()
        {
            if (mPass) {
                agfxComputePassEnd(mPass);
                mPass = nullptr;
            }
        }

        void TextureUAVBarrier(Texture& texture) { agfxComputePassTextureUAVBarrier(mPass, texture); }
        void BufferUAVBarrier(Buffer& buffer) { agfxComputePassBufferUAVBarrier(mPass, buffer); }
        /// @brief Overload for buffers this wrapper does not own -- notably
        /// IndirectBundle::CommandsBuffer()/CountBuffer(), which are raw handles by design.
        void BufferUAVBarrier(agfxBuffer* buffer) { agfxComputePassBufferUAVBarrier(mPass, buffer); }

        void CopyTextureToBuffer(Texture& texture, Buffer& buffer, uint64_t bufferOffset, const agfxTextureRegion& region, uint32_t mipLevel, uint32_t layer, uint32_t bytesPerRow, uint32_t bytesPerImage)
        {
            agfxComputePassCopyTextureToBuffer(mPass, texture, buffer, bufferOffset, &region, mipLevel, layer, bytesPerRow, bytesPerImage);
        }

        void CopyBufferToTexture(Buffer& buffer, uint64_t sourceOffset, Texture& texture, const agfxTextureRegion& region, uint32_t mipLevel, uint32_t layer, uint32_t bytesPerRow, uint32_t bytesPerImage)
        {
            agfxComputePassCopyBufferToTexture(mPass, buffer, sourceOffset, texture, &region, mipLevel, layer, bytesPerRow, bytesPerImage);
        }

        void CopyBufferToBuffer(Buffer& src, Buffer& dst, uint64_t srcOffset, uint64_t dstOffset, uint64_t size)
        {
            agfxComputePassCopyBufferToBuffer(mPass, src, dst, srcOffset, dstOffset, size);
        }

        void CopyTextureToTexture(Texture& src, Texture& dst, const agfxTextureRegion& region, uint32_t mipLevel, uint32_t layer)
        {
            agfxComputePassCopyTextureToTexture(mPass, src, dst, &region, mipLevel, layer);
        }

        void SetPipeline(ComputePipeline& pipeline) { agfxComputePassSetPipeline(mPass, pipeline); }
        void PushConstants(const void* data, uint32_t size) { agfxComputePassPushConstants(mPass, data, size); }
        template<typename T> void PushConstants(const T& data) { PushConstants(&data, sizeof(T)); }
        void Dispatch(uint32_t x, uint32_t y, uint32_t z) { agfxComputePassDispatch(mPass, x, y, z); }

        /// @brief Builds a bottom- or top-level acceleration structure into its final storage.
        void BuildAccelerationStructure(AccelerationStructure& as, Buffer& scratchBuffer, uint64_t scratchBufferOffset = 0)
        {
            agfxComputePassBuildAccelerationStructure(mPass, as, scratchBuffer, scratchBufferOffset);
        }

        /// @brief Refits an acceleration structure in place (requires allowUpdate at creation).
        void UpdateAccelerationStructure(AccelerationStructure& src, AccelerationStructure& dst, Buffer& scratchBuffer, uint64_t scratchBufferOffset = 0)
        {
            agfxComputePassUpdateAccelerationStructure(mPass, src, dst, scratchBuffer, scratchBufferOffset);
        }

        void CopyAccelerationStructure(AccelerationStructure& src, AccelerationStructure& dst)
        {
            agfxComputePassCopyAccelerationStructure(mPass, src, dst);
        }

        /// @brief Compacts a built acceleration structure into a smaller destination (created via CreateAccelerationStructureCompacted).
        void CompactAccelerationStructure(AccelerationStructure& src, AccelerationStructure& dst)
        {
            agfxComputePassCompactAccelerationStructure(mPass, src, dst);
        }

        /// @brief Writes the compacted size of a built acceleration structure into a buffer for later readback.
        void WriteCompactedSizeToBuffer(AccelerationStructure& as, Buffer& dstBuffer, uint64_t dstBufferOffset = 0)
        {
            agfxComputePassWriteCompactedSizeToBuffer(mPass, as, dstBuffer, dstBufferOffset);
        }

        /// @brief Prepares an indirect bundle for execution. No-op on D3D12; real work (ICB build)
        /// on Metal. Call within the same compute pass that culled/generated the bundle's commands.
        void PrepareIndirectBundle(IndirectBundle& bundle, const agfxIndirectBundleExecuteInfo& info)
        {
            agfxComputePassPrepareIndirectBundle(mPass, bundle, &info);
        }

        /// @brief Replays an IndirectBundleType::Dispatch bundle.
        void ExecuteIndirectBundle(IndirectBundle& bundle, const agfxIndirectBundleExecuteInfo& info)
        {
            agfxComputePassExecuteIndirectBundle(mPass, bundle, &info);
        }

        operator agfxComputePass*() const { return mPass; }

    private:
        agfxComputePass* mPass = nullptr;
    };

    /// @brief RAII scope guard around an agfxRenderPass. Ends the pass automatically when it goes out of scope.
    class RenderPass
    {
    public:
        RenderPass() = default;
        explicit RenderPass(agfxRenderPass* pass) : mPass(pass) {}
        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;
        RenderPass(RenderPass&& other) noexcept : mPass(other.mPass) { other.mPass = nullptr; }
        RenderPass& operator=(RenderPass&& other) noexcept
        {
            if (this != &other) { End(); mPass = other.mPass; other.mPass = nullptr; }
            return *this;
        }

        ~RenderPass() { End(); }

        void End()
        {
            if (mPass) {
                agfxRenderPassEnd(mPass);
                mPass = nullptr;
            }
        }

        void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f)
        {
            agfxRenderPassSetViewport(mPass, x, y, width, height, minDepth, maxDepth);
        }

        void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) { agfxRenderPassSetScissor(mPass, x, y, width, height); }
        void SetPipeline(RenderPipeline& pipeline) { agfxRenderPassSetPipeline(mPass, pipeline); }
        void PushConstants(const void* data, uint32_t size) { agfxRenderPassPushConstants(mPass, data, size); }
        template<typename T> void PushConstants(const T& data) { PushConstants(&data, sizeof(T)); }

        void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0)
        {
            agfxRenderPassDraw(mPass, vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void DrawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0)
        {
            agfxRenderPassDrawIndexed(mPass, indexBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

        void DrawMesh(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1)
        {
            agfxRenderPassDrawMesh(mPass, groupCountX, groupCountY, groupCountZ);
        }

        /// @brief Replays an IndirectBundleType::Draw/DrawIndexed/DrawMesh bundle.
        void ExecuteIndirectBundle(IndirectBundle& bundle, const agfxIndirectBundleExecuteInfo& info)
        {
            agfxRenderPassExecuteIndirectBundle(mPass, bundle, &info);
        }

        operator agfxRenderPass*() const { return mPass; }

    private:
        agfxRenderPass* mPass = nullptr;
    };

    class CommandBuffer : public Handle<agfxCommandBuffer, agfxCommandBufferDestroy>
    {
    public:
        using Handle::Handle;

        void Reset() { agfxCommandBufferReset(mHandle); }
        void Begin() { agfxCommandBufferBegin(mHandle); }
        void End() { agfxCommandBufferEnd(mHandle); }

        void TextureBarrier(Texture& texture, ResourceState oldState, ResourceState newState, uint32_t mip = AllMips, uint32_t layer = AllLayers, bool agglomerate = true)
        {
            agfxCommandBufferTextureBarrier(mHandle, texture, static_cast<agfxResourceState>(oldState), static_cast<agfxResourceState>(newState), mip, layer, agglomerate ? 1 : 0);
        }

        /// @brief Memory barrier ordering prior buffer/acceleration-structure GPU access in oldState against
        /// subsequent access in newState. Not scoped to a specific resource -- see agfxCommandBufferMemoryBarrier.
        void MemoryBarrier(ResourceState oldState, ResourceState newState, bool agglomerate = true)
        {
            agfxCommandBufferMemoryBarrier(mHandle, static_cast<agfxResourceState>(oldState), static_cast<agfxResourceState>(newState), agglomerate ? 1 : 0);
        }

        /// @brief Records the transition from one aliased texture to another sharing the same
        /// placement-heap memory -- see agfxCommandBufferAliasingBarrier. Buffers/acceleration
        /// structures need no equivalent overload: use MemoryBarrier directly for those.
        void AliasingBarrier(Texture& incomingTexture, ResourceState outgoingState, ResourceState incomingState, bool agglomerate = true)
        {
            agfxCommandBufferAliasingBarrier(mHandle, incomingTexture, static_cast<agfxResourceState>(outgoingState), static_cast<agfxResourceState>(incomingState), agglomerate ? 1 : 0);
        }

        void WriteTimestamp(QueryPool& pool, uint32_t index) { agfxCommandBufferWriteTimestamp(mHandle, pool, index); }

        void ResolveQueryPool(QueryPool& pool, uint32_t firstIndex, uint32_t count)
        {
            agfxCommandBufferResolveQueryPool(mHandle, pool, firstIndex, count);
        }

        /// @brief Begins a compute pass, returned as a scope guard that ends it automatically.
        [[nodiscard]] ComputePass BeginComputePass(const char* name)
        {
            return ComputePass(agfxComputePassBegin(mHandle, name));
        }

        /// @brief Begins a render pass, returned as a scope guard that ends it automatically.
        [[nodiscard]] RenderPass BeginRenderPass(const agfxRenderPassCreateInfo& info)
        {
            return RenderPass(agfxRenderPassBegin(mHandle, &info));
        }
    };

    class CommandQueue : public Handle<agfxCommandQueue, agfxCommandQueueDestroy>
    {
    public:
        using Handle::Handle;

        void Signal(Fence& fence, uint64_t value) { agfxCommandQueueSignal(mHandle, fence, value); }
        void Wait(Fence& fence, uint64_t value) { agfxCommandQueueWait(mHandle, fence, value); }

        /// @brief Submits up to 64 command buffers at once.
        void Submit(CommandBuffer* const* commandBuffers, uint32_t count)
        {
            agfxCommandBuffer* raw[64];
            count = count > 64 ? 64 : count;
            for (uint32_t i = 0; i < count; ++i) {
                raw[i] = commandBuffers[i]->Get();
            }
            agfxCommandQueueSubmit(mHandle, raw, count);
        }

        /// @brief Submits up to 64 command buffers at once, e.g. Submit({ &cmdA, &cmdB }).
        void Submit(std::initializer_list<CommandBuffer*> commandBuffers)
        {
            Submit(commandBuffers.begin(), static_cast<uint32_t>(commandBuffers.size()));
        }

        void Submit(CommandBuffer& commandBuffer)
        {
            agfxCommandBuffer* raw = commandBuffer;
            agfxCommandQueueSubmit(mHandle, &raw, 1);
        }
    };

    class SwapChain : public Handle<agfxSwapChain, agfxSwapChainDestroy>
    {
    public:
        using Handle::Handle;

        void Resize(uint32_t width, uint32_t height) { agfxSwapChainResize(mDevice, mHandle, width, height); }
        TextureFormat GetFormat() const { return static_cast<TextureFormat>(agfxSwapChainGetFormat(mHandle)); }

        /// @brief Acquires the next back buffer. The returned Texture is non-owning (via Texture::Borrow) --
        /// the swap chain still owns the underlying object and destroys it on its own schedule.
        Texture AcquireNextTexture() { return Texture::Borrow(mDevice, agfxSwapChainAcquireNextTexture(mHandle)); }

        void Present() { agfxSwapChainPresent(mHandle); }
    };

    /// @brief Owns and destroys an agfxDevice. The entry point for creating every other agfx object.
    class Device
    {
    public:
        Device() = default;

        explicit Device(const agfxDeviceCreateInfo& info) : mDevice(agfxDeviceCreate(&info)) {}

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        Device(Device&& other) noexcept : mDevice(other.mDevice) { other.mDevice = nullptr; }
        Device& operator=(Device&& other) noexcept
        {
            if (this != &other) {
                Reset();
                mDevice = other.mDevice;
                other.mDevice = nullptr;
            }
            return *this;
        }

        ~Device() { Reset(); }

        void Reset()
        {
            if (mDevice) {
                agfxDeviceDestroy(mDevice);
                mDevice = nullptr;
            }
        }

        agfxDevice* Get() const { return mDevice; }
        operator agfxDevice*() const { return mDevice; }
        explicit operator bool() const { return mDevice != nullptr; }

        agfxDeviceInfo GetInfo() const
        {
            agfxDeviceInfo info{};
            agfxDeviceGetInfo(mDevice, &info);
            return info;
        }

        void MakeResourcesResident() { agfxDeviceMakeResourcesResident(mDevice); }

        void WaitIdle() { agfxDeviceWaitIdle(mDevice); }

        Fence CreateFence() { return Fence(mDevice, agfxFenceCreate(mDevice)); }

        QueryPool CreateQueryPool(CommandQueue& queue, const agfxQueryPoolCreateInfo& info)
        {
            return QueryPool(mDevice, agfxQueryPoolCreate(mDevice, queue, &info));
        }

        CommandQueue CreateCommandQueue(CommandQueueType type)
        {
            agfxCommandQueueCreateInfo info{static_cast<agfxCommandQueueType>(type)};
            return CommandQueue(mDevice, agfxCommandQueueCreate(mDevice, &info));
        }

        CommandBuffer CreateCommandBuffer(CommandQueue& queue)
        {
            return CommandBuffer(mDevice, agfxCommandBufferCreate(mDevice, queue));
        }

        Texture CreateTexture(const agfxTextureCreateInfo& info)
        {
            return Texture(mDevice, agfxTextureCreate(mDevice, &info));
        }

        Buffer CreateBuffer(const agfxBufferCreateInfo& info)
        {
            return Buffer(mDevice, agfxBufferCreate(mDevice, &info));
        }

        Heap CreateHeap(const agfxHeapCreateInfo& info)
        {
            return Heap(mDevice, agfxHeapCreate(mDevice, &info));
        }

        agfxAllocationInfo GetTextureAllocationInfo(const agfxTextureCreateInfo& info) const
        {
            agfxAllocationInfo out{};
            agfxDeviceGetTextureAllocationInfo(mDevice, &info, &out);
            return out;
        }

        agfxAllocationInfo GetBufferAllocationInfo(const agfxBufferCreateInfo& info) const
        {
            agfxAllocationInfo out{};
            agfxDeviceGetBufferAllocationInfo(mDevice, &info, &out);
            return out;
        }

        TextureView CreateTextureView(const agfxTextureViewCreateInfo& info)
        {
            return TextureView(mDevice, agfxTextureViewCreate(mDevice, &info));
        }

        Sampler CreateSampler(const agfxSamplerCreateInfo& info)
        {
            return Sampler(mDevice, agfxSamplerCreate(mDevice, &info));
        }

        BufferView CreateBufferView(const agfxBufferViewCreateInfo& info)
        {
            return BufferView(mDevice, agfxBufferViewCreate(mDevice, &info));
        }

        RenderTarget CreateRenderTarget(const agfxRenderTargetCreateInfo& info)
        {
            return RenderTarget(mDevice, agfxRenderTargetCreate(mDevice, &info));
        }

        SwapChain CreateSwapChain(const agfxSwapChainCreateInfo& info)
        {
            return SwapChain(mDevice, agfxSwapChainCreate(mDevice, &info));
        }

        ShaderModule CreateShaderModule(const agfxShaderModuleCreateInfo& info)
        {
            return ShaderModule(mDevice, agfxShaderModuleCreate(mDevice, &info));
        }

        RenderPipeline CreateRenderPipeline(const agfxRenderPipelineCreateInfo& info)
        {
            return RenderPipeline(mDevice, agfxRenderPipelineCreate(mDevice, &info));
        }

        ComputePipeline CreateComputePipeline(const agfxComputePipelineCreateInfo& info)
        {
            return ComputePipeline(mDevice, agfxComputePipelineCreate(mDevice, &info));
        }

        AccelerationStructure CreateAccelerationStructure(const agfxAccelerationStructureCreateInfo& info)
        {
            return AccelerationStructure(mDevice, agfxAccelerationStructureCreate(mDevice, &info));
        }

        IndirectBundle CreateIndirectBundle(const agfxIndirectBundleCreateInfo& info)
        {
            return IndirectBundle(mDevice, agfxIndirectBundleCreate(mDevice, &info));
        }

        /// @brief Creates a destination acceleration structure sized for a compaction pass.
        /// @param compactedSize The size read back after WriteCompactedSizeToBuffer on the source.
        AccelerationStructure CreateAccelerationStructureCompacted(const agfxAccelerationStructureCreateInfo& info, uint64_t compactedSize)
        {
            return AccelerationStructure(mDevice, agfxAccelerationStructureCreateCompacted(mDevice, &info, compactedSize));
        }

    private:
        agfxDevice* mDevice = nullptr;
    };
}
