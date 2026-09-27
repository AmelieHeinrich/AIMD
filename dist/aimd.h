/**
 * AIMD -- single-header build. Generated from Include/ and Source/ by Scripts/amalgamate.lua: do not edit.
 *
 * In exactly one C++ file (C++17):
 *     #define AIMD_IMPLEMENTATION
 *     #include "aimd.h"
 * Everywhere else, C or C++, include it without the define.
 *
 * Needs agfx (<agfx/agfx.h>). The shaders are the two files next to this one: AIMD.hlsl and AIMDDebug.hlsli.
 */

#ifndef AIMD_H
#define AIMD_H

/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * AIMD -- Amélie's Immediate Mode Debug renderer.
 *
 * Usage:
 *   1. Compile AIMD.hlsl (entry points LineVS, PointVS, TriangleVS, MainPS) with agfx_shader and create the shader
 *      modules. AIMD takes ownership of them.
 *   2. aimdContextCreate() once. The new context becomes the current one.
 *   3. Anywhere during the frame, call the aimd* shape functions. They use the current style (see aimdPushStyle).
 *   4. aimdExecute() once per frame: uploads, renders into the given targets, and hands projected text
 *      labels to your callback. Batches are then cleared for the next frame.
 *
 * GPU-driven usage (aimdContextCreateInfo::enableGpu):
 *   1. After waiting for the frame's fence and beginning its command buffer, call aimdGpuBeginFrame(). It returns a
 *      bindless handle to pass to your shaders (e.g. through push constants).
 *   2. In compute shaders recorded on that command buffer, #include "AIMDDebug.hlsli" and draw:
 *          AIMDDebugRenderer renderer = AIMDDebugRenderer::Create(handle);
 *          renderer.SetColor(AIMDColor(float4(0, 1, 0, 1)));
 *          renderer.DrawBox(transform);
 *   3. aimdExecute() draws the GPU output together with the CPU batches, through indirect draws.
 *
 * Conventions:
 *   - Matrices are 16 floats, column-major, column vectors (glm layout): clip = P * V * world.
 *   - Clip-space depth is [0, 1]. Reverse-Z is supported through aimdExecuteInfo::reverseZ.
 *   - Colors are packed RGBA8, R in the lowest byte (same as ImGui's IM_COL32). Build them with AIMD_RGBA.
 *   - Line thickness and point size are in pixels.
 */

#include <stdint.h>
#include <agfx/agfx.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t aimdBool;
typedef struct aimdContext aimdContext;

#define AIMD_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

typedef struct aimdVec3 {
    float x, y, z;
} aimdVec3;

//
// Context
//

/// @brief Parameters for aimdContextCreate.
typedef struct aimdContextCreateInfo {
    agfxDevice* device;
    /// @brief Number of frames that may be in flight at once. aimdExecuteInfo::frameIndex must be < this.
    uint32_t framesInFlight;
    /// @brief Modules compiled from AIMD.hlsl. AIMD takes ownership and destroys them in aimdContextDestroy.
    agfxShaderModule* lineVertexShader;     // LineVS
    agfxShaderModule* pointVertexShader;    // PointVS
    agfxShaderModule* triangleVertexShader; // TriangleVS
    agfxShaderModule* fragmentShader;       // MainPS
    /// @brief Initial capacity of each GPU primitive buffer, in primitives. 0 picks a default. Buffers grow on demand.
    uint32_t initialCapacity;

    /// @brief Enables the GPU-driven path (aimdGpuBeginFrame + AIMDDebug.hlsli).
    aimdBool enableGpu;
    /// @brief FinalizeCS from AIMD.hlsl. Required when enableGpu is set. AIMD takes ownership.
    agfxShaderModule* finalizeComputeShader;
    /// @brief Initial capacity of each GPU-written region (triangle vertices, lines or points, per depth bucket).
    ///        0 picks a default. Shapes that don't fit are dropped.
    uint32_t gpuInitialCapacity;
    /// @brief Reads the GPU counters back every frame (a few bytes) and grows the regions when shapes were dropped.
    ///        Growth takes effect framesInFlight frames after the overflow.
    aimdBool gpuAutoGrow;
    /// @brief Upper bound for auto-grow, per region, in slots of 32 bytes. 0 picks a default (2M slots, 64 MB).
    uint32_t gpuMaxCapacity;
} aimdContextCreateInfo;

/// @brief Creates a context and makes it current.
aimdContext* aimdContextCreate(const aimdContextCreateInfo* createInfo);
/// @brief Destroys a context. The GPU must no longer be using any frame it rendered (e.g. agfxDeviceWaitIdle first).
void aimdContextDestroy(aimdContext* context);
void aimdSetCurrentContext(aimdContext* context);
aimdContext* aimdGetCurrentContext(void);

//
// Style
//

typedef enum aimdStyleFlags {
    AIMD_STYLE_NONE             = 0,
    /// @brief Shapes are drawn as solid triangles instead of wireframe.
    AIMD_STYLE_FILLED           = 1 << 0,
    /// @brief Ignore the depth target; draw on top of everything.
    AIMD_STYLE_NO_DEPTH_TEST    = 1 << 1,
    /// @brief Points are drawn as discs instead of squares.
    AIMD_STYLE_ROUND_POINTS     = 1 << 2,
    /// @brief aimdStyle::textSize is in pixels instead of world units (labels no longer shrink with distance).
    AIMD_STYLE_TEXT_PIXEL_SIZE  = 1 << 3,
    /// @brief Filled shapes get basic lighting (key light + hemisphere ambient) instead of a flat color.
    ///        Round shapes are smooth shaded, flat ones use their face normal.
    AIMD_STYLE_SHADED           = 1 << 4
} aimdStyleFlags;

typedef struct aimdStyle {
    uint32_t color;     // AIMD_RGBA
    float thickness;    // Line thickness in pixels
    float pointSize;    // Point diameter in pixels
    uint32_t segments;  // Tessellation of round shapes (circles, spheres, cones, cylinders). Clamped to [3, 256].
    float textSize;     // Text height, in world units or pixels (see AIMD_STYLE_TEXT_PIXEL_SIZE)
    uint32_t flags;     // aimdStyleFlags
} aimdStyle;

/// @brief Default style: opaque white, 2px lines, 6px points, 24 segments, 0.25 world-unit text, wireframe, depth tested.
aimdStyle aimdDefaultStyle(void);

/// @brief Pushes a copy of the given style, making it current until the matching aimdPopStyle.
void aimdPushStyle(const aimdStyle* style);
void aimdPopStyle(void);
/// @brief Returns the current style. Modifications through the pointer apply to the current stack entry.
aimdStyle* aimdGetStyle(void);

// Shortcuts that modify the current stack entry.
void aimdSetColor(uint32_t color);
void aimdSetThickness(float thickness);
void aimdSetPointSize(float pointSize);
void aimdSetFilled(aimdBool filled);
void aimdSetDepthTest(aimdBool depthTest);

//
// Primitives
//

void aimdLine(aimdVec3 a, aimdVec3 b);
void aimdPolyline(const aimdVec3* points, uint32_t count, aimdBool closed);
void aimdPoint(aimdVec3 p);
/// @brief Filled triangle when AIMD_STYLE_FILLED is set, outline otherwise.
void aimdTriangle(aimdVec3 a, aimdVec3 b, aimdVec3 c);
/// @brief Quad with corners in winding order. Filled or outlined like aimdTriangle.
void aimdQuad(aimdVec3 a, aimdVec3 b, aimdVec3 c, aimdVec3 d);

//
// Shapes
//

// Closed shapes (box, AABB, frustum, cone, cylinder, sphere, arrow heads) only draw their camera-facing side when filled,
// regardless of winding order. Open ones (triangle, quad, circle, ring) are two-sided.

/// @brief Unit cube ([-0.5, 0.5]^3) transformed by the given matrix (oriented box).
void aimdBox(const float transform[16]);
void aimdAABB(aimdVec3 min, aimdVec3 max);
/// @brief Frustum whose corners are the NDC cube ([-1,1]x[-1,1]x[0,1]) transformed by invViewProjection.
void aimdFrustum(const float invViewProjection[16]);
void aimdCone(aimdVec3 apex, aimdVec3 baseCenter, float radius);
void aimdCylinder(aimdVec3 a, aimdVec3 b, float radius);
/// @brief Line from 'from' to 'to' with a cone head of length headSize. Only the head is filled with AIMD_STYLE_FILLED.
void aimdArrow(aimdVec3 from, aimdVec3 to, float headSize);
/// @brief X/Y/Z arrows (red/green/blue) of the given length along the transform's basis. Ignores the style color.
void aimdAxes(const float transform[16], float size);
void aimdCircle(aimdVec3 center, aimdVec3 normal, float radius);
/// @brief Annulus between two radii. Outlined as two circles when not filled.
void aimdRing(aimdVec3 center, aimdVec3 normal, float innerRadius, float outerRadius);
/// @brief Three axis-aligned circles (a rotation-gizmo look), colored X/Y/Z. Always outlined; ignores the style color.
void aimdRings(aimdVec3 center, float radius);
/// @brief UV sphere with segments / 2 rings and 'segments' sectors (see aimdStyle::segments).
void aimdSphere(aimdVec3 center, float radius);
/// @brief UV sphere with explicit tessellation: 'rings' latitude bands (>= 2) and 'sectors' longitude slices (>= 3).
///        The wireframe draws exactly the tessellation's edges.
void aimdSphereEx(aimdVec3 center, float radius, uint32_t rings, uint32_t sectors);
/// @brief Square grid of cells x cells, of total width 'size', lying in the plane with the given normal. Always lines.
void aimdGrid(aimdVec3 center, aimdVec3 normal, float size, uint32_t cells);

//
// Text
//

/// @brief Queues a text label anchored at a 3D position. AIMD does not draw glyphs itself: at execute time it projects
///        the anchor and passes an aimdTextCommand to aimdExecuteInfo::textCallback, which draws with any UI library.
void aimdText(aimdVec3 position, const char* text);
void aimdTextf(aimdVec3 position, const char* format, ...);

typedef struct aimdTextCommand {
    const char* text;   // Valid until the callback returns
    float x, y;         // Anchor in pixels, top-left origin. Center the text on it.
    float pixelHeight;  // Font size to draw with
    float depth;        // Clip-space w (view distance), for sorting or fading
    uint32_t color;     // AIMD_RGBA
} aimdTextCommand;

/// @brief Receives every visible label of the frame, sorted back to front.
typedef void (*aimdTextCallback)(const aimdTextCommand* commands, uint32_t count, void* userData);

//
// Execution
//

typedef struct aimdExecuteInfo {
    /// @brief Command buffer to record into. AIMD begins and ends its own render pass on it.
    agfxCommandBuffer* commandBuffer;
    /// @brief Must be in AGFX_RESOURCE_STATE_RENDER_TARGET. Contents are loaded, not cleared.
    agfxRenderTarget* colorTarget;
    agfxTextureFormat colorFormat;
    /// @brief Optional. Must be in AGFX_RESOURCE_STATE_DEPTH_WRITE. Without it, everything draws without depth testing.
    agfxRenderTarget* depthTarget;
    agfxTextureFormat depthFormat;
    uint32_t width, height;
    float view[16];
    float projection[16];
    /// @brief Depth compare becomes GREATER_EQUAL instead of LESS_EQUAL.
    aimdBool reverseZ;
    /// @brief Whether debug geometry writes depth. Off by default so it doesn't disturb the scene.
    ///        GPU-driven primitives always write depth: they are appended in a nondeterministic order, so overlaps
    ///        would flicker otherwise. (Translucent GPU shapes still blend in that order.)
    aimdBool depthWrite;
    /// @brief Index of the frame in flight, in [0, framesInFlight). Selects which upload buffers are written.
    uint32_t frameIndex;
    /// @brief Optional. Called once with every visible label, after rendering.
    aimdTextCallback textCallback;
    void* textUserData;
} aimdExecuteInfo;

/// @brief Renders everything queued since the last execute, then clears the batches.
void aimdExecute(const aimdExecuteInfo* executeInfo);

typedef struct aimdStats {
    uint32_t lines;     // Line segments
    uint32_t points;
    uint32_t triangles;
    uint32_t texts;     // Queued labels
    uint32_t drawCalls;

    // GPU-driven path, from the latest counter readback (framesInFlight frames old). Zero without gpuAutoGrow.
    uint32_t gpuLines;      // Requested, including dropped ones
    uint32_t gpuPoints;
    uint32_t gpuTriangles;
    aimdBool gpuOverflow;   // Some shapes did not fit and were dropped; the regions are growing
} aimdStats;

/// @brief Stats of the last aimdExecute.
void aimdGetStats(aimdStats* stats);

//
// GPU-driven rendering
//

#define AIMD_GPU_INVALID_HANDLE 0xFFFFFFFFu

/// @brief Starts a GPU-driven frame: applies pending growth, resets the GPU counters (recorded on commandBuffer) and
///        returns the handle to pass to AIMDDebugRenderer::Create. Returns AIMD_GPU_INVALID_HANDLE if GPU mode is disabled.
/// @note  Call it after waiting for frameIndex's previous submission, on the command buffer that will also receive
///        your shape-emitting compute passes and then aimdExecute (same frameIndex). No barriers are needed around
///        your compute passes: AIMD records them.
uint32_t aimdGpuBeginFrame(agfxCommandBuffer* commandBuffer, uint32_t frameIndex);

#ifdef __cplusplus
}
#endif

#endif // AIMD_H

#if defined(AIMD_IMPLEMENTATION) && !defined(AIMD_IMPLEMENTATION_INCLUDED)
#define AIMD_IMPLEMENTATION_INCLUDED

#ifndef __cplusplus
#error "AIMD_IMPLEMENTATION must be defined in a C++ file"
#endif

// --------------------------------------------------------------------------------------------------------------------
// Source/aimd_internal.h
// --------------------------------------------------------------------------------------------------------------------

/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#include <vector>

// GPU primitive layouts. Must match Shaders/AIMD.hlsl.
struct aimdGpuLine {
    float p0[3];
    uint32_t color;
    float p1[3];
    float thickness;
};
static_assert(sizeof(aimdGpuLine) == 32, "aimdGpuLine layout must match AIMD.hlsl");

enum aimdGpuPointFlags : uint32_t {
    AIMD_GPU_POINT_ROUND = 1 << 0
};

struct aimdGpuPoint {
    float p[3];
    uint32_t color;
    float size;
    uint32_t flags;
    float pad[2];
};
static_assert(sizeof(aimdGpuPoint) == 32, "aimdGpuPoint layout must match AIMD.hlsl");

enum aimdGpuVertexFlags : uint32_t {
    AIMD_GPU_VERTEX_SHADED = 1 << 0,
    // Part of a closed surface with outward normals: faces pointing away from the camera are discarded.
    AIMD_GPU_VERTEX_CLOSED = 1 << 1
};

struct aimdGpuVertex {
    float p[3];
    uint32_t color;
    float n[3]; // Zero: the shader uses the flat face normal instead
    uint32_t flags;
};
static_assert(sizeof(aimdGpuVertex) == 32, "aimdGpuVertex layout must match AIMD.hlsl");

struct aimdPushConstants {
    float viewProjection[16];
    float viewportSize[2];
    uint32_t buffer;
    uint32_t firstPrimitive;
    float depthBias;
    uint32_t flags; // Bit 0: reverse-Z
    uint32_t pad[2];
    float cameraPosition[3];
    float pad2;
};
static_assert(sizeof(aimdPushConstants) <= 128, "Push constants are limited to 128 bytes");

enum aimdPrimitiveKind {
    AIMD_KIND_TRIANGLES,
    AIMD_KIND_LINES,
    AIMD_KIND_POINTS,
    AIMD_KIND_COUNT
};

// Depth-tested and always-on-top primitives are kept in separate buckets and uploaded back to back.
enum aimdBucket {
    AIMD_BUCKET_DEPTH,
    AIMD_BUCKET_NO_DEPTH,
    AIMD_BUCKET_COUNT
};

struct aimdTextEntry {
    aimdVec3 position;
    uint32_t textOffset; // Into aimdContext::textArena
    uint32_t color;
    float size;
    bool pixelSize;
};

struct aimdGpuBuffer {
    agfxBuffer* buffer = nullptr;
    agfxBufferView* view = nullptr;
    uint64_t capacity = 0; // In elements
};

//
// GPU-driven path
//

// One region per (kind, bucket): region = kind * AIMD_BUCKET_COUNT + bucket. Triangle regions count vertices.
static constexpr uint32_t AIMD_REGION_COUNT = (uint32_t)AIMD_KIND_COUNT * (uint32_t)AIMD_BUCKET_COUNT;
constexpr uint32_t aimdRegion(int kind, int bucket) {
    return (uint32_t)kind * AIMD_BUCKET_COUNT + (uint32_t)bucket;
}
// Every GPU element type (aimdGpuVertex, aimdGpuLine, aimdGpuPoint) is 32 bytes, so all regions share one buffer.
static constexpr uint32_t AIMD_GPU_SLOT_SIZE = 32;
static constexpr uint32_t AIMD_GPU_COUNTERS_SIZE = 32;

// Header read by AIMDDebugRenderer::Create (Shaders/AIMDDebug.hlsli) and FinalizeCS (Shaders/AIMD.hlsl).
struct aimdGpuHeader {
    uint32_t geometry;                          // Writeable RAW view of the geometry buffer
    uint32_t counters;                          // Writeable RAW view of the counter buffer
    uint32_t regionOffset[AIMD_REGION_COUNT];   // In slots
    uint32_t regionCapacity[AIMD_REGION_COUNT]; // In slots
    uint32_t flags;
    uint32_t pad;
};
static_assert(sizeof(aimdGpuHeader) == 64, "aimdGpuHeader layout must match AIMDDebug.hlsli and AIMD.hlsl");

struct aimdFinalizeConstants {
    uint32_t header;
    uint32_t commands;
    uint32_t counts;
    uint32_t pad;
};

struct aimdGpuGeometry {
    agfxBuffer* buffer = nullptr;
    agfxBufferView* readView = nullptr;  // STRUCTURED, for AIMD's vertex shaders
    agfxBufferView* writeView = nullptr; // RAW writeable, for the user's shaders
};

struct aimdRetiredGeometry {
    aimdGpuGeometry geometry;
    uint64_t destroyAt; // Value of aimdGpuState::beginCount at which the GPU can no longer be using it
};

struct aimdGpuState {
    bool enabled = false;
    bool autoGrow = false;
    uint32_t maxCapacity = 0;
    agfxShaderModule* finalizeShader = nullptr;
    agfxComputePipeline* finalizePipeline = nullptr;

    aimdGpuGeometry geometry;
    uint32_t regionOffset[AIMD_REGION_COUNT] = {};
    uint32_t regionCapacity[AIMD_REGION_COUNT] = {};
    std::vector<aimdRetiredGeometry> retired;

    agfxBuffer* counters = nullptr;
    agfxBufferView* countersView = nullptr;
    agfxBuffer* zero = nullptr;
    agfxIndirectBundle* bundle = nullptr;

    // Per frame in flight
    std::vector<agfxBuffer*> headers;
    std::vector<agfxBufferView*> headerViews;
    std::vector<agfxBuffer*> readbacks;
    std::vector<bool> readbackPending;

    uint64_t beginCount = 0;
    bool frameActive = false;
    uint32_t activeFrameIndex = 0;

    // Latest readback
    uint32_t requested[AIMD_REGION_COUNT] = {};
    bool overflow = false;
};

struct aimdPipelineKey {
    aimdPrimitiveKind kind;
    agfxTextureFormat colorFormat;
    agfxTextureFormat depthFormat;
    bool depthTest;
    bool depthWrite;
    bool reverseZ;

    bool operator==(const aimdPipelineKey& o) const {
        return kind == o.kind && colorFormat == o.colorFormat && depthFormat == o.depthFormat &&
               depthTest == o.depthTest && depthWrite == o.depthWrite && reverseZ == o.reverseZ;
    }
};

struct aimdCachedPipeline {
    aimdPipelineKey key;
    agfxRenderPipeline* pipeline;
};

struct aimdContext {
    agfxDevice* device = nullptr;
    uint32_t framesInFlight = 0;
    agfxShaderModule* vertexShaders[AIMD_KIND_COUNT] = {};
    agfxShaderModule* fragmentShader = nullptr;

    std::vector<aimdStyle> styleStack;

    // CPU batches
    std::vector<aimdGpuVertex> triangles[AIMD_BUCKET_COUNT]; // 3 vertices per triangle
    std::vector<aimdGpuLine> lines[AIMD_BUCKET_COUNT];
    std::vector<aimdGpuPoint> points[AIMD_BUCKET_COUNT];
    std::vector<aimdTextEntry> texts;
    std::vector<char> textArena;
    std::vector<aimdTextCommand> textCommands;

    // Upload buffers for the CPU batches, indexed [frameIndex * AIMD_KIND_COUNT + kind]. A frame's buffers are only
    // rewritten once the caller has waited for that frame index's previous submission, so they can be grown in place.
    std::vector<aimdGpuBuffer> uploadBuffers;
    uint64_t initialCapacity = 0;

    aimdGpuState gpu;

    std::vector<aimdCachedPipeline> pipelines;

    aimdStats stats = {};
};

// Implemented in aimd_gpu.cpp, used by aimd.cpp.
bool aimdGpuCreate(aimdContext* ctx, const aimdContextCreateInfo* createInfo);
void aimdGpuDestroy(aimdContext* ctx);
// Whether aimdGpuBeginFrame was called for this frame index and its output still has to be drawn.
bool aimdGpuFrameActive(aimdContext* ctx, uint32_t frameIndex);
// Before the render pass: turns the GPU counters into indirect draws and prepares one per region.
void aimdGpuPrepare(aimdContext* ctx, agfxCommandBuffer* commandBuffer, const agfxIndirectBundleExecuteInfo executeInfos[AIMD_REGION_COUNT]);
// After the render pass: reads the counters back for auto-grow and ends the GPU frame.
void aimdGpuFinish(aimdContext* ctx, agfxCommandBuffer* commandBuffer);

// Implemented in aimd.cpp, used by aimd_shapes.cpp. All of them assume a current context.
aimdContext* aimdCurrent();
void aimdEmitLine(aimdContext* ctx, const aimdStyle& style, aimdVec3 a, aimdVec3 b, uint32_t color);
void aimdEmitTriangle(aimdContext* ctx, const aimdStyle& style, aimdVec3 a, aimdVec3 b, aimdVec3 c, uint32_t color);
// Same, with per-vertex normals (smooth shading under AIMD_STYLE_SHADED). When 'closed' is set the normals must point
// out of a closed surface, and the shader culls back faces with them, whatever the winding order.
void aimdEmitTriangleN(aimdContext* ctx, const aimdStyle& style, aimdVec3 a, aimdVec3 b, aimdVec3 c,
                       aimdVec3 na, aimdVec3 nb, aimdVec3 nc, uint32_t color, bool closed);

// --------------------------------------------------------------------------------------------------------------------
// Source/aimd.cpp
// --------------------------------------------------------------------------------------------------------------------

/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static aimdContext* sAimdCurrentContext = nullptr;

// Distance-relative depth bias applied to lines and points so wireframes drawn over their own filled shape win.
static constexpr float AIMD_LINE_DEPTH_BIAS = 0.002f;
static constexpr uint64_t AIMD_DEFAULT_CAPACITY = 4096;

aimdContext* aimdCurrent() {
    return sAimdCurrentContext;
}

//
// Context
//

aimdContext* aimdContextCreate(const aimdContextCreateInfo* createInfo) {
    if (!createInfo || !createInfo->device || !createInfo->lineVertexShader || !createInfo->pointVertexShader ||
        !createInfo->triangleVertexShader || !createInfo->fragmentShader)
        return nullptr;

    aimdContext* ctx = new aimdContext();
    ctx->device = createInfo->device;
    ctx->framesInFlight = createInfo->framesInFlight ? createInfo->framesInFlight : 1;
    ctx->vertexShaders[AIMD_KIND_TRIANGLES] = createInfo->triangleVertexShader;
    ctx->vertexShaders[AIMD_KIND_LINES] = createInfo->lineVertexShader;
    ctx->vertexShaders[AIMD_KIND_POINTS] = createInfo->pointVertexShader;
    ctx->fragmentShader = createInfo->fragmentShader;
    ctx->initialCapacity = createInfo->initialCapacity ? createInfo->initialCapacity : AIMD_DEFAULT_CAPACITY;
    ctx->uploadBuffers.resize(ctx->framesInFlight * AIMD_KIND_COUNT);
    ctx->styleStack.push_back(aimdDefaultStyle());

    if (!aimdGpuCreate(ctx, createInfo)) {
        aimdContextDestroy(ctx);
        return nullptr;
    }

    sAimdCurrentContext = ctx;
    return ctx;
}

static void aimdDestroyGpuBuffer(aimdContext* ctx, aimdGpuBuffer& buffer) {
    if (buffer.view)
        agfxBufferViewDestroy(ctx->device, buffer.view);
    if (buffer.buffer)
        agfxBufferDestroy(ctx->device, buffer.buffer);
    buffer = {};
}

void aimdContextDestroy(aimdContext* ctx) {
    if (!ctx)
        return;

    aimdGpuDestroy(ctx);
    for (aimdGpuBuffer& buffer : ctx->uploadBuffers)
        aimdDestroyGpuBuffer(ctx, buffer);
    for (aimdCachedPipeline& cached : ctx->pipelines)
        agfxRenderPipelineDestroy(ctx->device, cached.pipeline);
    for (agfxShaderModule* module : ctx->vertexShaders)
        agfxShaderModuleDestroy(ctx->device, module);
    agfxShaderModuleDestroy(ctx->device, ctx->fragmentShader);

    if (sAimdCurrentContext == ctx)
        sAimdCurrentContext = nullptr;
    delete ctx;
}

void aimdSetCurrentContext(aimdContext* context) {
    sAimdCurrentContext = context;
}

aimdContext* aimdGetCurrentContext(void) {
    return sAimdCurrentContext;
}

//
// Style
//

aimdStyle aimdDefaultStyle(void) {
    aimdStyle style = {};
    style.color = AIMD_RGBA(255, 255, 255, 255);
    style.thickness = 2.0f;
    style.pointSize = 6.0f;
    style.segments = 24;
    style.textSize = 0.25f;
    style.flags = AIMD_STYLE_NONE;
    return style;
}

void aimdPushStyle(const aimdStyle* style) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx)
        return;
    ctx->styleStack.push_back(style ? *style : ctx->styleStack.back());
}

void aimdPopStyle(void) {
    aimdContext* ctx = aimdCurrent();
    if (ctx && ctx->styleStack.size() > 1)
        ctx->styleStack.pop_back();
}

aimdStyle* aimdGetStyle(void) {
    aimdContext* ctx = aimdCurrent();
    return ctx ? &ctx->styleStack.back() : nullptr;
}

static void aimdSetFlag(uint32_t flag, bool enabled) {
    if (aimdStyle* style = aimdGetStyle())
        style->flags = enabled ? (style->flags | flag) : (style->flags & ~flag);
}

void aimdSetColor(uint32_t color) {
    if (aimdStyle* style = aimdGetStyle())
        style->color = color;
}

void aimdSetThickness(float thickness) {
    if (aimdStyle* style = aimdGetStyle())
        style->thickness = thickness;
}

void aimdSetPointSize(float pointSize) {
    if (aimdStyle* style = aimdGetStyle())
        style->pointSize = pointSize;
}

void aimdSetFilled(aimdBool filled) {
    aimdSetFlag(AIMD_STYLE_FILLED, filled != 0);
}

void aimdSetDepthTest(aimdBool depthTest) {
    aimdSetFlag(AIMD_STYLE_NO_DEPTH_TEST, depthTest == 0);
}

//
// Primitives
//

static aimdBucket aimdStyleBucket(const aimdStyle& style) {
    return (style.flags & AIMD_STYLE_NO_DEPTH_TEST) ? AIMD_BUCKET_NO_DEPTH : AIMD_BUCKET_DEPTH;
}

void aimdEmitLine(aimdContext* ctx, const aimdStyle& style, aimdVec3 a, aimdVec3 b, uint32_t color) {
    aimdGpuLine line = { { a.x, a.y, a.z }, color, { b.x, b.y, b.z }, style.thickness };
    ctx->lines[aimdStyleBucket(style)].push_back(line);
}

void aimdEmitTriangleN(aimdContext* ctx, const aimdStyle& style, aimdVec3 a, aimdVec3 b, aimdVec3 c,
                       aimdVec3 na, aimdVec3 nb, aimdVec3 nc, uint32_t color, bool closed) {
    std::vector<aimdGpuVertex>& tris = ctx->triangles[aimdStyleBucket(style)];
    uint32_t flags = (style.flags & AIMD_STYLE_SHADED) ? AIMD_GPU_VERTEX_SHADED : 0;
    if (closed)
        flags |= AIMD_GPU_VERTEX_CLOSED;
    tris.push_back({ { a.x, a.y, a.z }, color, { na.x, na.y, na.z }, flags });
    tris.push_back({ { b.x, b.y, b.z }, color, { nb.x, nb.y, nb.z }, flags });
    tris.push_back({ { c.x, c.y, c.z }, color, { nc.x, nc.y, nc.z }, flags });
}

void aimdEmitTriangle(aimdContext* ctx, const aimdStyle& style, aimdVec3 a, aimdVec3 b, aimdVec3 c, uint32_t color) {
    const aimdVec3 zero = { 0.0f, 0.0f, 0.0f };
    aimdEmitTriangleN(ctx, style, a, b, c, zero, zero, zero, color, false);
}

void aimdLine(aimdVec3 a, aimdVec3 b) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx)
        return;
    const aimdStyle& style = ctx->styleStack.back();
    aimdEmitLine(ctx, style, a, b, style.color);
}

void aimdPolyline(const aimdVec3* points, uint32_t count, aimdBool closed) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx || !points || count < 2)
        return;
    const aimdStyle& style = ctx->styleStack.back();
    for (uint32_t i = 0; i + 1 < count; ++i)
        aimdEmitLine(ctx, style, points[i], points[i + 1], style.color);
    if (closed && count > 2)
        aimdEmitLine(ctx, style, points[count - 1], points[0], style.color);
}

void aimdPoint(aimdVec3 p) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx)
        return;
    const aimdStyle& style = ctx->styleStack.back();
    aimdGpuPoint point = {};
    point.p[0] = p.x;
    point.p[1] = p.y;
    point.p[2] = p.z;
    point.color = style.color;
    point.size = style.pointSize;
    point.flags = (style.flags & AIMD_STYLE_ROUND_POINTS) ? AIMD_GPU_POINT_ROUND : 0;
    ctx->points[aimdStyleBucket(style)].push_back(point);
}

void aimdTriangle(aimdVec3 a, aimdVec3 b, aimdVec3 c) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx)
        return;
    const aimdStyle& style = ctx->styleStack.back();
    if (style.flags & AIMD_STYLE_FILLED) {
        aimdEmitTriangle(ctx, style, a, b, c, style.color);
    } else {
        aimdEmitLine(ctx, style, a, b, style.color);
        aimdEmitLine(ctx, style, b, c, style.color);
        aimdEmitLine(ctx, style, c, a, style.color);
    }
}

void aimdQuad(aimdVec3 a, aimdVec3 b, aimdVec3 c, aimdVec3 d) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx)
        return;
    const aimdStyle& style = ctx->styleStack.back();
    if (style.flags & AIMD_STYLE_FILLED) {
        aimdEmitTriangle(ctx, style, a, b, c, style.color);
        aimdEmitTriangle(ctx, style, a, c, d, style.color);
    } else {
        aimdEmitLine(ctx, style, a, b, style.color);
        aimdEmitLine(ctx, style, b, c, style.color);
        aimdEmitLine(ctx, style, c, d, style.color);
        aimdEmitLine(ctx, style, d, a, style.color);
    }
}

//
// Text
//

void aimdText(aimdVec3 position, const char* text) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx || !text || !*text)
        return;
    const aimdStyle& style = ctx->styleStack.back();

    aimdTextEntry entry = {};
    entry.position = position;
    entry.textOffset = (uint32_t)ctx->textArena.size();
    entry.color = style.color;
    entry.size = style.textSize;
    entry.pixelSize = (style.flags & AIMD_STYLE_TEXT_PIXEL_SIZE) != 0;
    ctx->texts.push_back(entry);

    size_t length = strlen(text);
    ctx->textArena.insert(ctx->textArena.end(), text, text + length + 1);
}

void aimdTextf(aimdVec3 position, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    aimdText(position, buffer);
}

//
// Execution
//

// Column-major 4x4 multiply: out = a * b.
static void aimdMatMul(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                             a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

static agfxRenderPipeline* aimdGetPipeline(aimdContext* ctx, const aimdPipelineKey& key) {
    for (const aimdCachedPipeline& cached : ctx->pipelines)
        if (cached.key == key)
            return cached.pipeline;

    static const char* kNames[AIMD_KIND_COUNT] = { "AIMD Triangles", "AIMD Lines", "AIMD Points" };

    agfxRenderPipelineCreateInfo info = {};
    info.name = kNames[key.kind];
    // Executed through an indirect bundle in GPU mode (required on Metal)
    info.supportsIndirect = ctx->gpu.enabled ? 1 : 0;
    info.fillMode = AGFX_FILL_MODE_SOLID;
    info.cullMode = AGFX_CULL_MODE_NONE;
    info.frontFace = AGFX_FRONT_FACE_COUNTER_CLOCKWISE;
    info.topology = AGFX_TOPOLOGY_TRIANGLES;
    info.depthFormat = key.depthFormat;
    info.depthTestEnable = key.depthTest;
    info.depthWriteEnable = key.depthTest && key.depthWrite;
    info.depthCompareOp = key.reverseZ ? AGFX_COMPARISON_FUNCTION_GREATER_EQUAL : AGFX_COMPARISON_FUNCTION_LESS_EQUAL;
    info.colorAttachmentCount = 1;
    info.colorFormats[0] = key.colorFormat;
    info.blendEnable[0] = 1;
    info.srcColorBlendFactor[0] = AGFX_BLEND_FACTOR_SRC_ALPHA;
    info.dstColorBlendFactor[0] = AGFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    info.colorBlendOp[0] = AGFX_BLEND_OPERATION_ADD;
    info.srcAlphaBlendFactor[0] = AGFX_BLEND_FACTOR_ONE;
    info.dstAlphaBlendFactor[0] = AGFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    info.alphaBlendOp[0] = AGFX_BLEND_OPERATION_ADD;
    info.vertexShader = ctx->vertexShaders[key.kind];
    info.fragmentShader = ctx->fragmentShader;

    agfxRenderPipeline* pipeline = agfxRenderPipelineCreate(ctx->device, &info);
    if (pipeline)
        ctx->pipelines.push_back({ key, pipeline });
    return pipeline;
}

// Makes sure the frame's buffer for 'kind' holds 'count' elements of 'stride' bytes. Returns true if it was (re)created.
static bool aimdEnsureCapacity(aimdContext* ctx, aimdGpuBuffer& buffer, uint64_t count, uint64_t stride, const char* name) {
    if (buffer.buffer && buffer.capacity >= count)
        return false;

    uint64_t capacity = std::max<uint64_t>(ctx->initialCapacity, buffer.capacity + buffer.capacity / 2);
    capacity = std::max(capacity, count);
    aimdDestroyGpuBuffer(ctx, buffer);

    agfxBufferCreateInfo bufferInfo = {};
    bufferInfo.size = capacity * stride;
    bufferInfo.stride = stride;
    bufferInfo.usage = AGFX_BUFFER_USAGE_SHADER_READ;
    bufferInfo.memoryType = AGFX_BUFFER_MEMORY_TYPE_CPU_TO_GPU;
    buffer.buffer = agfxBufferCreate(ctx->device, &bufferInfo);
    if (!buffer.buffer)
        return false;
    agfxBufferSetName(buffer.buffer, name);

    agfxBufferViewCreateInfo viewInfo = {};
    viewInfo.buffer = buffer.buffer;
    viewInfo.type = AGFX_BUFFER_VIEW_TYPE_STRUCTURED;
    buffer.view = agfxBufferViewCreate(ctx->device, &viewInfo);
    buffer.capacity = capacity;
    return true;
}

template <typename T>
static bool aimdUpload(aimdContext* ctx, aimdGpuBuffer& buffer, const std::vector<T> (&buckets)[AIMD_BUCKET_COUNT], const char* name) {
    uint64_t count = buckets[AIMD_BUCKET_DEPTH].size() + buckets[AIMD_BUCKET_NO_DEPTH].size();
    if (count == 0)
        return false;

    bool created = aimdEnsureCapacity(ctx, buffer, count, sizeof(T), name);
    if (!buffer.buffer)
        return false;

    uint8_t* dst = (uint8_t*)agfxBufferMap(buffer.buffer);
    size_t depthBytes = buckets[AIMD_BUCKET_DEPTH].size() * sizeof(T);
    size_t noDepthBytes = buckets[AIMD_BUCKET_NO_DEPTH].size() * sizeof(T);
    if (depthBytes)
        memcpy(dst, buckets[AIMD_BUCKET_DEPTH].data(), depthBytes);
    if (noDepthBytes)
        memcpy(dst + depthBytes, buckets[AIMD_BUCKET_NO_DEPTH].data(), noDepthBytes);
    agfxBufferUnmap(buffer.buffer);
    return created;
}

static void aimdBuildTextCommands(aimdContext* ctx, const aimdExecuteInfo* info, const float viewProjection[16]) {
    ctx->textCommands.clear();
    const float* vp = viewProjection;
    const float width = (float)info->width;
    const float height = (float)info->height;
    const float projScaleY = info->projection[5];

    for (const aimdTextEntry& entry : ctx->texts) {
        const aimdVec3& p = entry.position;
        float cx = vp[0] * p.x + vp[4] * p.y + vp[8] * p.z + vp[12];
        float cy = vp[1] * p.x + vp[5] * p.y + vp[9] * p.z + vp[13];
        float cw = vp[3] * p.x + vp[7] * p.y + vp[11] * p.z + vp[15];
        if (cw <= 1e-4f)
            continue;

        float pixelHeight = entry.pixelSize ? entry.size : entry.size * projScaleY * 0.5f * height / cw;
        if (pixelHeight < 1.0f)
            continue;

        float x = (cx / cw * 0.5f + 0.5f) * width;
        float y = (0.5f - cy / cw * 0.5f) * height;
        // Generous margin: the anchor is the text center, so part of a label can be visible while its anchor is not.
        float margin = pixelHeight * 8.0f;
        if (x < -margin || x > width + margin || y < -margin || y > height + margin)
            continue;

        aimdTextCommand command = {};
        command.text = ctx->textArena.data() + entry.textOffset;
        command.x = x;
        command.y = y;
        command.pixelHeight = pixelHeight;
        command.depth = cw;
        command.color = entry.color;
        ctx->textCommands.push_back(command);
    }

    std::stable_sort(ctx->textCommands.begin(), ctx->textCommands.end(),
                     [](const aimdTextCommand& a, const aimdTextCommand& b) { return a.depth > b.depth; });
}

static void aimdClearBatches(aimdContext* ctx) {
    for (int bucket = 0; bucket < AIMD_BUCKET_COUNT; ++bucket) {
        ctx->triangles[bucket].clear();
        ctx->lines[bucket].clear();
        ctx->points[bucket].clear();
    }
    ctx->texts.clear();
    ctx->textArena.clear();
    // Keep only the base style so a missing aimdPopStyle can't leak into the next frame.
    ctx->styleStack.resize(1);
}

void aimdExecute(const aimdExecuteInfo* info) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx || !info || !info->commandBuffer || !info->colorTarget || info->width == 0 || info->height == 0 ||
        info->frameIndex >= ctx->framesInFlight) {
        if (ctx) {
            ctx->gpu.frameActive = false;
            aimdClearBatches(ctx);
        }
        return;
    }

    aimdStats stats = {};
    for (int bucket = 0; bucket < AIMD_BUCKET_COUNT; ++bucket) {
        stats.triangles += (uint32_t)(ctx->triangles[bucket].size() / 3);
        stats.lines += (uint32_t)ctx->lines[bucket].size();
        stats.points += (uint32_t)ctx->points[bucket].size();
    }
    stats.texts = (uint32_t)ctx->texts.size();

    float viewProjection[16];
    aimdMatMul(info->projection, info->view, viewProjection);

    // Upload
    aimdGpuBuffer* frameBuffers = &ctx->uploadBuffers[info->frameIndex * AIMD_KIND_COUNT];
    bool created = false;
    created |= aimdUpload(ctx, frameBuffers[AIMD_KIND_TRIANGLES], ctx->triangles, "AIMD Triangles");
    created |= aimdUpload(ctx, frameBuffers[AIMD_KIND_LINES], ctx->lines, "AIMD Lines");
    created |= aimdUpload(ctx, frameBuffers[AIMD_KIND_POINTS], ctx->points, "AIMD Points");
    if (created)
        agfxDeviceMakeResourcesResident(ctx->device);

    const uint32_t counts[AIMD_KIND_COUNT][AIMD_BUCKET_COUNT] = {
        { (uint32_t)ctx->triangles[0].size(), (uint32_t)ctx->triangles[1].size() },
        { (uint32_t)ctx->lines[0].size(), (uint32_t)ctx->lines[1].size() },
        { (uint32_t)ctx->points[0].size(), (uint32_t)ctx->points[1].size() },
    };

    const bool hasDepth = info->depthTarget != nullptr && info->depthFormat != AGFX_TEXTURE_FORMAT_UNKNOWN;
    const bool gpuActive = aimdGpuFrameActive(ctx, info->frameIndex);

    aimdPushConstants pc = {};
    memcpy(pc.viewProjection, viewProjection, sizeof(viewProjection));
    pc.viewportSize[0] = (float)info->width;
    pc.viewportSize[1] = (float)info->height;
    pc.flags = info->reverseZ ? 1u : 0u;
    // Camera position from the (rigid) view matrix: -R^T * t
    const float* v = info->view;
    pc.cameraPosition[0] = -(v[0] * v[12] + v[1] * v[13] + v[2] * v[14]);
    pc.cameraPosition[1] = -(v[4] * v[12] + v[5] * v[13] + v[6] * v[14]);
    pc.cameraPosition[2] = -(v[8] * v[12] + v[9] * v[13] + v[10] * v[14]);

    auto pipelineFor = [&](int kind, int bucket, bool gpuDriven) {
        aimdPipelineKey key = {};
        key.kind = (aimdPrimitiveKind)kind;
        key.colorFormat = info->colorFormat;
        key.depthFormat = hasDepth ? info->depthFormat : AGFX_TEXTURE_FORMAT_UNKNOWN;
        key.depthTest = hasDepth && bucket == AIMD_BUCKET_DEPTH;
        // GPU-written primitives land in the buffer in a different order every frame (atomics), so wherever they
        // overlap the result would flicker with draw order: they always write depth, making the nearest one win.
        key.depthWrite = info->depthWrite != 0 || gpuDriven;
        key.reverseZ = info->reverseZ != 0;
        return aimdGetPipeline(ctx, key);
    };

    // GPU-written regions: one indirect draw each. The same execute info must reach Prepare and Execute.
    agfxIndirectBundleExecuteInfo gpuDraws[AIMD_REGION_COUNT] = {};
    bool gpuReady = gpuActive;
    if (gpuActive) {
        for (int kind = 0; kind < AIMD_KIND_COUNT && gpuReady; ++kind) {
            for (int bucket = 0; bucket < AIMD_BUCKET_COUNT; ++bucket) {
                uint32_t region = aimdRegion(kind, bucket);
                aimdPushConstants regionPc = pc;
                regionPc.buffer = (uint32_t)agfxBufferViewGetHandle(ctx->gpu.geometry.readView);
                regionPc.firstPrimitive = ctx->gpu.regionOffset[region];
                regionPc.depthBias = kind == AIMD_KIND_TRIANGLES ? 0.0f : AIMD_LINE_DEPTH_BIAS;

                agfxIndirectBundleExecuteInfo& draw = gpuDraws[region];
                draw.countIndex = region;
                draw.commandOffset = region;
                draw.maxCommandCount = 1;
                memcpy(draw.pushConstants, &regionPc, sizeof(regionPc));
                draw.renderPipeline = pipelineFor(kind, bucket, true);
                gpuReady &= draw.renderPipeline != nullptr;
            }
        }
        if (gpuReady)
            aimdGpuPrepare(ctx, info->commandBuffer, gpuDraws);
    }

    if (stats.triangles + stats.lines + stats.points > 0 || gpuReady) {
        agfxRenderPassCreateInfo passInfo = {};
        passInfo.name = "AIMD";
        passInfo.width = info->width;
        passInfo.height = info->height;
        passInfo.colorAttachmentCount = 1;
        passInfo.colorAttachments[0].renderTarget = info->colorTarget;
        passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_LOAD;
        passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
        if (hasDepth) {
            passInfo.hasDepthAttachment = 1;
            passInfo.depthAttachment.renderTarget = info->depthTarget;
            passInfo.depthAttachment.loadOp = AGFX_LOAD_OPERATION_LOAD;
            passInfo.depthAttachment.storeOp = AGFX_STORE_OPERATION_STORE;
        }

        agfxRenderPass* pass = agfxRenderPassBegin(info->commandBuffer, &passInfo);
        agfxRenderPassSetViewport(pass, 0.0f, 0.0f, (float)info->width, (float)info->height, 0.0f, 1.0f);
        agfxRenderPassSetScissor(pass, 0, 0, info->width, info->height);

        // Depth-tested first so always-on-top geometry lands over it. Within a bucket: triangles, lines, points.
        for (int bucket = 0; bucket < AIMD_BUCKET_COUNT; ++bucket) {
            for (int kind = 0; kind < AIMD_KIND_COUNT; ++kind) {
                uint32_t count = counts[kind][bucket];
                if (count > 0) {
                    if (agfxRenderPipeline* pipeline = pipelineFor(kind, bucket, false)) {
                        pc.buffer = (uint32_t)agfxBufferViewGetHandle(frameBuffers[kind].view);
                        pc.firstPrimitive = bucket == AIMD_BUCKET_DEPTH ? 0 : counts[kind][AIMD_BUCKET_DEPTH];
                        pc.depthBias = kind == AIMD_KIND_TRIANGLES ? 0.0f : AIMD_LINE_DEPTH_BIAS;

                        agfxRenderPassSetPipeline(pass, pipeline);
                        agfxRenderPassPushConstants(pass, &pc, sizeof(pc));
                        // Triangles are stored as raw vertices; lines and points expand to a 6-vertex quad each.
                        agfxRenderPassDraw(pass, kind == AIMD_KIND_TRIANGLES ? count : count * 6, 1, 0, 0);
                        stats.drawCalls++;
                    }
                }

                if (gpuReady) {
                    agfxRenderPassExecuteIndirectBundle(pass, ctx->gpu.bundle, &gpuDraws[aimdRegion(kind, bucket)]);
                    stats.drawCalls++;
                }
            }
        }

        agfxRenderPassEnd(pass);
    }

    if (gpuReady)
        aimdGpuFinish(ctx, info->commandBuffer);
    ctx->gpu.frameActive = false;

    if (ctx->gpu.enabled) {
        const uint32_t* requested = ctx->gpu.requested;
        auto requestedKind = [&](int kind) { return requested[aimdRegion(kind, AIMD_BUCKET_DEPTH)] + requested[aimdRegion(kind, AIMD_BUCKET_NO_DEPTH)]; };
        stats.gpuTriangles = requestedKind(AIMD_KIND_TRIANGLES) / 3;
        stats.gpuLines = requestedKind(AIMD_KIND_LINES);
        stats.gpuPoints = requestedKind(AIMD_KIND_POINTS);
        stats.gpuOverflow = ctx->gpu.overflow ? 1 : 0;
    }

    if (info->textCallback && !ctx->texts.empty()) {
        aimdBuildTextCommands(ctx, info, viewProjection);
        if (!ctx->textCommands.empty())
            info->textCallback(ctx->textCommands.data(), (uint32_t)ctx->textCommands.size(), info->textUserData);
    }

    ctx->stats = stats;
    aimdClearBatches(ctx);
}

void aimdGetStats(aimdStats* stats) {
    aimdContext* ctx = aimdCurrent();
    if (stats)
        *stats = ctx ? ctx->stats : aimdStats{};
}

// --------------------------------------------------------------------------------------------------------------------
// Source/aimd_gpu.cpp
// --------------------------------------------------------------------------------------------------------------------

/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * GPU-driven path: user shaders append primitives into AIMD-owned buffers (Shaders/AIMDDebug.hlsli), FinalizeCS turns
 * the counters into indirect draw commands, and aimdExecute draws them with an agfx indirect bundle.
 *
 * Frame timeline on the command buffer:
 *   aimdGpuBeginFrame   copy zeros -> counters
 *   user compute        InterlockedAdd on counters, stores into the geometry buffer
 *   aimdGpuPrepare      FinalizeCS writes the bundle commands/counts, then one Prepare per region
 *   render pass         one ExecuteIndirectBundle per region
 *   aimdGpuFinish       copy counters -> readback (auto-grow only)
 */

#include <algorithm>
#include <cstring>

static constexpr uint32_t AIMD_DEFAULT_GPU_CAPACITY = 16384;
static constexpr uint32_t AIMD_DEFAULT_GPU_MAX_CAPACITY = 2u * 1024u * 1024u;
static constexpr uint32_t AIMD_FINALIZE_GROUP_SIZE = 8; // Must match [numthreads] of FinalizeCS

static void aimdDestroyGeometry(aimdContext* ctx, aimdGpuGeometry& geometry) {
    if (geometry.readView)
        agfxBufferViewDestroy(ctx->device, geometry.readView);
    if (geometry.writeView)
        agfxBufferViewDestroy(ctx->device, geometry.writeView);
    if (geometry.buffer)
        agfxBufferDestroy(ctx->device, geometry.buffer);
    geometry = {};
}

// (Re)creates the geometry buffer for the current region capacities.
static bool aimdCreateGeometry(aimdContext* ctx) {
    aimdGpuState& gpu = ctx->gpu;

    uint64_t totalSlots = 0;
    for (uint32_t r = 0; r < AIMD_REGION_COUNT; ++r) {
        gpu.regionOffset[r] = (uint32_t)totalSlots;
        totalSlots += gpu.regionCapacity[r];
    }

    agfxBufferCreateInfo bufferInfo = {};
    bufferInfo.size = totalSlots * AIMD_GPU_SLOT_SIZE;
    bufferInfo.stride = AIMD_GPU_SLOT_SIZE;
    bufferInfo.usage = (agfxBufferUsage)(AGFX_BUFFER_USAGE_SHADER_READ | AGFX_BUFFER_USAGE_SHADER_WRITE);
    bufferInfo.memoryType = AGFX_BUFFER_MEMORY_TYPE_GPU_ONLY;
    gpu.geometry.buffer = agfxBufferCreate(ctx->device, &bufferInfo);
    if (!gpu.geometry.buffer)
        return false;
    agfxBufferSetName(gpu.geometry.buffer, "AIMD GPU Geometry");

    agfxBufferViewCreateInfo viewInfo = {};
    viewInfo.buffer = gpu.geometry.buffer;
    viewInfo.type = AGFX_BUFFER_VIEW_TYPE_STRUCTURED;
    gpu.geometry.readView = agfxBufferViewCreate(ctx->device, &viewInfo);

    viewInfo.type = AGFX_BUFFER_VIEW_TYPE_RAW;
    viewInfo.writeable = 1;
    gpu.geometry.writeView = agfxBufferViewCreate(ctx->device, &viewInfo);

    agfxDeviceMakeResourcesResident(ctx->device);
    return gpu.geometry.readView && gpu.geometry.writeView;
}

static agfxBuffer* aimdCreateSmallBuffer(aimdContext* ctx, uint64_t size, uint64_t stride, agfxBufferUsage usage,
                                         agfxBufferMemoryType memoryType, const char* name) {
    agfxBufferCreateInfo info = {};
    info.size = size;
    info.stride = stride;
    info.usage = usage;
    info.memoryType = memoryType;
    agfxBuffer* buffer = agfxBufferCreate(ctx->device, &info);
    if (buffer)
        agfxBufferSetName(buffer, name);
    return buffer;
}

static agfxBufferView* aimdCreateRawView(aimdContext* ctx, agfxBuffer* buffer, bool writeable) {
    agfxBufferViewCreateInfo info = {};
    info.buffer = buffer;
    info.type = AGFX_BUFFER_VIEW_TYPE_RAW;
    info.writeable = writeable ? 1 : 0;
    return agfxBufferViewCreate(ctx->device, &info);
}

bool aimdGpuCreate(aimdContext* ctx, const aimdContextCreateInfo* createInfo) {
    aimdGpuState& gpu = ctx->gpu;
    if (!createInfo->enableGpu) {
        // Owned either way
        if (createInfo->finalizeComputeShader)
            agfxShaderModuleDestroy(ctx->device, createInfo->finalizeComputeShader);
        return true;
    }
    if (!createInfo->finalizeComputeShader)
        return false;

    gpu.enabled = true;
    gpu.autoGrow = createInfo->gpuAutoGrow != 0;
    gpu.maxCapacity = createInfo->gpuMaxCapacity ? createInfo->gpuMaxCapacity : AIMD_DEFAULT_GPU_MAX_CAPACITY;
    gpu.finalizeShader = createInfo->finalizeComputeShader;

    agfxComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.name = "AIMD Finalize";
    pipelineInfo.computeShader = gpu.finalizeShader;
    pipelineInfo.groupSizeX = AIMD_FINALIZE_GROUP_SIZE;
    pipelineInfo.groupSizeY = 1;
    pipelineInfo.groupSizeZ = 1;
    gpu.finalizePipeline = agfxComputePipelineCreate(ctx->device, &pipelineInfo);
    if (!gpu.finalizePipeline)
        return false;

    uint32_t capacity = createInfo->gpuInitialCapacity ? createInfo->gpuInitialCapacity : AIMD_DEFAULT_GPU_CAPACITY;
    capacity = std::min(capacity, gpu.maxCapacity / 3);
    for (uint32_t r = 0; r < AIMD_REGION_COUNT; ++r)
        gpu.regionCapacity[r] = capacity;
    // Triangle regions hold whole triangles
    gpu.regionCapacity[aimdRegion(AIMD_KIND_TRIANGLES, AIMD_BUCKET_DEPTH)] = capacity * 3;
    gpu.regionCapacity[aimdRegion(AIMD_KIND_TRIANGLES, AIMD_BUCKET_NO_DEPTH)] = capacity * 3;
    if (!aimdCreateGeometry(ctx))
        return false;

    const agfxBufferUsage readWrite = (agfxBufferUsage)(AGFX_BUFFER_USAGE_SHADER_READ | AGFX_BUFFER_USAGE_SHADER_WRITE);
    gpu.counters = aimdCreateSmallBuffer(ctx, AIMD_GPU_COUNTERS_SIZE, 4, readWrite, AGFX_BUFFER_MEMORY_TYPE_GPU_ONLY, "AIMD GPU Counters");
    gpu.zero = aimdCreateSmallBuffer(ctx, AIMD_GPU_COUNTERS_SIZE, 4, AGFX_BUFFER_USAGE_SHADER_READ, AGFX_BUFFER_MEMORY_TYPE_CPU_TO_GPU, "AIMD GPU Zero");
    if (!gpu.counters || !gpu.zero)
        return false;
    gpu.countersView = aimdCreateRawView(ctx, gpu.counters, true);
    memset(agfxBufferMap(gpu.zero), 0, AIMD_GPU_COUNTERS_SIZE);
    agfxBufferUnmap(gpu.zero);

    agfxIndirectBundleCreateInfo bundleInfo = {};
    bundleInfo.type = AGFX_INDIRECT_BUNDLE_TYPE_DRAW;
    bundleInfo.maxCommandCount = AIMD_REGION_COUNT;
    bundleInfo.maxCountCount = AIMD_REGION_COUNT;
    gpu.bundle = agfxIndirectBundleCreate(ctx->device, &bundleInfo);
    if (!gpu.bundle)
        return false;

    gpu.headers.resize(ctx->framesInFlight, nullptr);
    gpu.headerViews.resize(ctx->framesInFlight, nullptr);
    gpu.readbacks.resize(ctx->framesInFlight, nullptr);
    gpu.readbackPending.resize(ctx->framesInFlight, false);
    for (uint32_t i = 0; i < ctx->framesInFlight; ++i) {
        gpu.headers[i] = aimdCreateSmallBuffer(ctx, sizeof(aimdGpuHeader), 4, AGFX_BUFFER_USAGE_SHADER_READ, AGFX_BUFFER_MEMORY_TYPE_CPU_TO_GPU, "AIMD GPU Header");
        if (!gpu.headers[i])
            return false;
        gpu.headerViews[i] = aimdCreateRawView(ctx, gpu.headers[i], false);
        if (gpu.autoGrow) {
            gpu.readbacks[i] = aimdCreateSmallBuffer(ctx, AIMD_GPU_COUNTERS_SIZE, 4, (agfxBufferUsage)0, AGFX_BUFFER_MEMORY_TYPE_GPU_TO_CPU, "AIMD GPU Readback");
            if (!gpu.readbacks[i])
                return false;
        }
    }

    agfxDeviceMakeResourcesResident(ctx->device);
    return true;
}

void aimdGpuDestroy(aimdContext* ctx) {
    aimdGpuState& gpu = ctx->gpu;

    aimdDestroyGeometry(ctx, gpu.geometry);
    for (aimdRetiredGeometry& retired : gpu.retired)
        aimdDestroyGeometry(ctx, retired.geometry);
    gpu.retired.clear();

    for (size_t i = 0; i < gpu.headers.size(); ++i) {
        if (gpu.headerViews[i])
            agfxBufferViewDestroy(ctx->device, gpu.headerViews[i]);
        if (gpu.headers[i])
            agfxBufferDestroy(ctx->device, gpu.headers[i]);
        if (gpu.readbacks[i])
            agfxBufferDestroy(ctx->device, gpu.readbacks[i]);
    }
    if (gpu.countersView)
        agfxBufferViewDestroy(ctx->device, gpu.countersView);
    if (gpu.counters)
        agfxBufferDestroy(ctx->device, gpu.counters);
    if (gpu.zero)
        agfxBufferDestroy(ctx->device, gpu.zero);
    if (gpu.bundle)
        agfxIndirectBundleDestroy(ctx->device, gpu.bundle);
    if (gpu.finalizePipeline)
        agfxComputePipelineDestroy(ctx->device, gpu.finalizePipeline);
    if (gpu.finalizeShader)
        agfxShaderModuleDestroy(ctx->device, gpu.finalizeShader);
    gpu = aimdGpuState();
}

// Reads the counters written framesInFlight frames ago and grows the regions that overflowed.
static void aimdGpuApplyReadback(aimdContext* ctx, uint32_t frameIndex) {
    aimdGpuState& gpu = ctx->gpu;
    if (!gpu.autoGrow || !gpu.readbackPending[frameIndex])
        return;
    gpu.readbackPending[frameIndex] = false;

    memcpy(gpu.requested, agfxBufferMap(gpu.readbacks[frameIndex]), sizeof(gpu.requested));
    agfxBufferUnmap(gpu.readbacks[frameIndex]);

    bool grow = false;
    uint32_t newCapacity[AIMD_REGION_COUNT];
    for (uint32_t r = 0; r < AIMD_REGION_COUNT; ++r) {
        newCapacity[r] = gpu.regionCapacity[r];
        if (gpu.requested[r] > gpu.regionCapacity[r]) {
            uint64_t wanted = std::max<uint64_t>((uint64_t)gpu.requested[r] * 3 / 2, (uint64_t)gpu.regionCapacity[r] * 3 / 2);
            wanted = std::min<uint64_t>(wanted, gpu.maxCapacity);
            if (r / AIMD_BUCKET_COUNT == AIMD_KIND_TRIANGLES)
                wanted = wanted / 3 * 3;
            if (wanted > gpu.regionCapacity[r]) {
                newCapacity[r] = (uint32_t)wanted;
                grow = true;
            }
        }
    }
    gpu.overflow = false;
    for (uint32_t r = 0; r < AIMD_REGION_COUNT; ++r)
        gpu.overflow |= gpu.requested[r] > gpu.regionCapacity[r];
    if (!grow)
        return;

    // Allocate the new buffer first so a failed allocation keeps the current one.
    aimdGpuGeometry previous = gpu.geometry;
    uint32_t previousCapacity[AIMD_REGION_COUNT];
    uint32_t previousOffset[AIMD_REGION_COUNT];
    memcpy(previousCapacity, gpu.regionCapacity, sizeof(previousCapacity));
    memcpy(previousOffset, gpu.regionOffset, sizeof(previousOffset));

    gpu.geometry = {};
    memcpy(gpu.regionCapacity, newCapacity, sizeof(newCapacity));
    if (!aimdCreateGeometry(ctx)) {
        aimdDestroyGeometry(ctx, gpu.geometry);
        gpu.geometry = previous;
        memcpy(gpu.regionCapacity, previousCapacity, sizeof(previousCapacity));
        memcpy(gpu.regionOffset, previousOffset, sizeof(previousOffset));
        return;
    }
    // Frames still in flight may be reading the previous buffer: retire it instead of destroying it.
    gpu.retired.push_back({ previous, gpu.beginCount + ctx->framesInFlight });
}

bool aimdGpuFrameActive(aimdContext* ctx, uint32_t frameIndex) {
    return ctx->gpu.enabled && ctx->gpu.frameActive && ctx->gpu.activeFrameIndex == frameIndex && ctx->gpu.geometry.buffer;
}

uint32_t aimdGpuBeginFrame(agfxCommandBuffer* commandBuffer, uint32_t frameIndex) {
    aimdContext* ctx = aimdCurrent();
    if (!ctx || !ctx->gpu.enabled || !commandBuffer || frameIndex >= ctx->framesInFlight)
        return AIMD_GPU_INVALID_HANDLE;
    aimdGpuState& gpu = ctx->gpu;

    gpu.beginCount++;
    for (size_t i = 0; i < gpu.retired.size();) {
        if (gpu.retired[i].destroyAt <= gpu.beginCount) {
            aimdDestroyGeometry(ctx, gpu.retired[i].geometry);
            gpu.retired.erase(gpu.retired.begin() + i);
        } else {
            ++i;
        }
    }

    aimdGpuApplyReadback(ctx, frameIndex);
    if (!gpu.geometry.buffer)
        return AIMD_GPU_INVALID_HANDLE;

    aimdGpuHeader header = {};
    header.geometry = (uint32_t)agfxBufferViewGetHandle(gpu.geometry.writeView);
    header.counters = (uint32_t)agfxBufferViewGetHandle(gpu.countersView);
    memcpy(header.regionOffset, gpu.regionOffset, sizeof(header.regionOffset));
    memcpy(header.regionCapacity, gpu.regionCapacity, sizeof(header.regionCapacity));
    memcpy(agfxBufferMap(gpu.headers[frameIndex]), &header, sizeof(header));
    agfxBufferUnmap(gpu.headers[frameIndex]);

    // Reset the counters. Last frame, the geometry was read by vertex shaders. The counters were left in
    // COPY_SOURCE if autoGrow read them back in aimdGpuFinish, or in UNORDERED_ACCESS otherwise (the state
    // the compute pass left them in) -- the barrier here must match whichever actually happened.
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, AGFX_RESOURCE_STATE_COPY_DEST, 1);
    // Buffer-scoped (not the global barrier above): gpu.counters is a real CopyBufferRegion source/dest,
    // and the D3D12 debug layer's legacy state validator for that only updates from a barrier naming the
    // resource -- see agfxCommandBufferBufferBarrier's doc comment.
    agfxCommandBufferBufferBarrier(commandBuffer, gpu.counters, gpu.autoGrow ? AGFX_RESOURCE_STATE_COPY_SOURCE : AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_COPY_DEST, 1);
    agfxComputePass* pass = agfxComputePassBegin(commandBuffer, "AIMD GPU Begin");
    agfxComputePassCopyBufferToBuffer(pass, gpu.zero, gpu.counters, 0, 0, AIMD_GPU_COUNTERS_SIZE);
    agfxComputePassEnd(pass);
    // Flushed when the user's compute pass begins
    agfxCommandBufferBufferBarrier(commandBuffer, gpu.counters, AGFX_RESOURCE_STATE_COPY_DEST, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, 1);

    gpu.frameActive = true;
    gpu.activeFrameIndex = frameIndex;
    return (uint32_t)agfxBufferViewGetHandle(gpu.headerViews[frameIndex]);
}

void aimdGpuPrepare(aimdContext* ctx, agfxCommandBuffer* commandBuffer, const agfxIndirectBundleExecuteInfo executeInfos[AIMD_REGION_COUNT]) {
    aimdGpuState& gpu = ctx->gpu;

    // Before FinalizeCS and Prepare rewrite the bundle: the user's writes must be done, and so must last frame's
    // indirect draws. Those read the bundle's commands in the vertex stage, but on Metal their pixel shaders also read
    // the per-command push constants that Prepare regenerates (INDIRECT_ARGUMENT alone doesn't cover fragment).
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, 1);
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, 1);
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, 1);

    uint64_t bundleHandle = agfxIndirectBundleGetHandle(gpu.bundle);
    aimdFinalizeConstants constants = {};
    constants.header = (uint32_t)agfxBufferViewGetHandle(gpu.headerViews[gpu.activeFrameIndex]);
    constants.commands = (uint32_t)(bundleHandle & 0xFFFFFFFFu);
    constants.counts = (uint32_t)(bundleHandle >> 32);

    agfxComputePass* pass = agfxComputePassBegin(commandBuffer, "AIMD GPU Finalize");
    agfxComputePassSetPipeline(pass, gpu.finalizePipeline);
    agfxComputePassPushConstants(pass, &constants, sizeof(constants));
    agfxComputePassDispatch(pass, 1, 1, 1);
    // Recorded inside the pass, so it applies right away: FinalizeCS output -> Prepare
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT, 1);
    for (uint32_t r = 0; r < AIMD_REGION_COUNT; ++r)
        agfxComputePassPrepareIndirectBundle(pass, gpu.bundle, &executeInfos[r]);
    agfxComputePassEnd(pass);

    // Prepare's output (Metal) and the geometry, before the render pass
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT, 1);
    agfxCommandBufferMemoryBarrier(commandBuffer, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 1);
}

void aimdGpuFinish(aimdContext* ctx, agfxCommandBuffer* commandBuffer) {
    aimdGpuState& gpu = ctx->gpu;
    uint32_t frameIndex = gpu.activeFrameIndex;

    if (gpu.autoGrow) {
        agfxCommandBufferBufferBarrier(commandBuffer, gpu.counters, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_COPY_SOURCE, 1);
        agfxComputePass* pass = agfxComputePassBegin(commandBuffer, "AIMD GPU Readback");
        agfxComputePassCopyBufferToBuffer(pass, gpu.counters, gpu.readbacks[frameIndex], 0, 0, AIMD_GPU_COUNTERS_SIZE);
        agfxComputePassEnd(pass);
        gpu.readbackPending[frameIndex] = true;
    }
    gpu.frameActive = false;
}

// --------------------------------------------------------------------------------------------------------------------
// Source/aimd_shapes.cpp
// --------------------------------------------------------------------------------------------------------------------

/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * CPU tessellation of every AIMD shape into lines and triangles.
 */

#include <cmath>
#include <algorithm>

static constexpr float AIMD_PI = 3.14159265358979323846f;
static constexpr uint32_t AIMD_COLOR_X = AIMD_RGBA(235, 64, 52, 255);
static constexpr uint32_t AIMD_COLOR_Y = AIMD_RGBA(90, 200, 70, 255);
static constexpr uint32_t AIMD_COLOR_Z = AIMD_RGBA(60, 120, 240, 255);

//
// Math helpers
//

static aimdVec3 operator+(aimdVec3 a, aimdVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static aimdVec3 operator-(aimdVec3 a, aimdVec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static aimdVec3 operator*(aimdVec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float aimdDot(aimdVec3 a, aimdVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float aimdLength(aimdVec3 a) { return std::sqrt(aimdDot(a, a)); }
static aimdVec3 aimdCross(aimdVec3 a, aimdVec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static aimdVec3 aimdNormalize(aimdVec3 a) {
    float len = aimdLength(a);
    return len > 1e-12f ? a * (1.0f / len) : aimdVec3{ 0.0f, 1.0f, 0.0f };
}

// Column-major transform of a point, without the perspective divide.
static aimdVec3 aimdTransformPoint(const float m[16], aimdVec3 p) {
    return { m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
             m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
             m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] };
}

// Two unit vectors perpendicular to n and to each other.
static void aimdBasis(aimdVec3 n, aimdVec3& u, aimdVec3& v) {
    n = aimdNormalize(n);
    aimdVec3 helper = std::fabs(n.x) > 0.9f ? aimdVec3{ 0.0f, 1.0f, 0.0f } : aimdVec3{ 1.0f, 0.0f, 0.0f };
    u = aimdNormalize(aimdCross(helper, n));
    v = aimdCross(n, u);
}

static uint32_t aimdSegmentCount(const aimdStyle& style) {
    return std::clamp<uint32_t>(style.segments, 3, 256);
}

static bool aimdIsFilled(const aimdStyle& style) {
    return (style.flags & AIMD_STYLE_FILLED) != 0;
}

//
// Shared tessellation
//

// Corner i has x from bit 0, y from bit 1, z from bit 2.
static const int AIMD_BOX_EDGES[12][2] = {
    { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, // along x
    { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, // along y
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // along z
};
static const int AIMD_BOX_FACES[6][4] = {
    { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, // -x, +x
    { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, // -y, +y
    { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, // -z, +z
};

static void aimdEmitHexahedron(aimdContext* ctx, const aimdStyle& style, const aimdVec3 corners[8]) {
    if (aimdIsFilled(style)) {
        aimdVec3 center = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 8; ++i)
            center = center + corners[i] * 0.125f;

        for (const auto& face : AIMD_BOX_FACES) {
            const aimdVec3 &a = corners[face[0]], &b = corners[face[1]], &c = corners[face[2]], &d = corners[face[3]];
            // Orient the face normal away from the center, so any transform (even a mirroring one) works.
            aimdVec3 n = aimdNormalize(aimdCross(c - a, b - a) + aimdCross(d - a, c - a));
            if (aimdDot(n, (a + c) * 0.5f - center) < 0.0f)
                n = n * -1.0f;
            aimdEmitTriangleN(ctx, style, a, b, c, n, n, n, style.color, true);
            aimdEmitTriangleN(ctx, style, a, c, d, n, n, n, style.color, true);
        }
    } else {
        for (const auto& edge : AIMD_BOX_EDGES)
            aimdEmitLine(ctx, style, corners[edge[0]], corners[edge[1]], style.color);
    }
}

// Unit direction of circle point i (may be fractional, for mid-segment normals).
static aimdVec3 aimdCircleDir(aimdVec3 u, aimdVec3 v, float i, uint32_t segments) {
    float angle = 2.0f * AIMD_PI * i / (float)segments;
    return u * std::cos(angle) + v * std::sin(angle);
}

static aimdVec3 aimdCirclePoint(aimdVec3 center, aimdVec3 u, aimdVec3 v, float radius, uint32_t i, uint32_t segments) {
    return center + aimdCircleDir(u, v, (float)i, segments) * radius;
}

static void aimdEmitCircleOutline(aimdContext* ctx, const aimdStyle& style, aimdVec3 center, aimdVec3 u, aimdVec3 v, float radius, uint32_t color) {
    uint32_t segments = aimdSegmentCount(style);
    aimdVec3 prev = aimdCirclePoint(center, u, v, radius, 0, segments);
    for (uint32_t i = 1; i <= segments; ++i) {
        aimdVec3 next = aimdCirclePoint(center, u, v, radius, i, segments);
        aimdEmitLine(ctx, style, prev, next, color);
        prev = next;
    }
}

// Two-sided disc, or one side of a closed shape when 'outward' is given.
static void aimdEmitDisc(aimdContext* ctx, const aimdStyle& style, aimdVec3 center, aimdVec3 u, aimdVec3 v, float radius, uint32_t color,
                     const aimdVec3* outward = nullptr) {
    uint32_t segments = aimdSegmentCount(style);
    aimdVec3 prev = aimdCirclePoint(center, u, v, radius, 0, segments);
    for (uint32_t i = 1; i <= segments; ++i) {
        aimdVec3 next = aimdCirclePoint(center, u, v, radius, i, segments);
        if (outward)
            aimdEmitTriangleN(ctx, style, center, prev, next, *outward, *outward, *outward, color, true);
        else
            aimdEmitTriangle(ctx, style, center, prev, next, color);
        prev = next;
    }
}

// Number of apex/side lines drawn on wireframe cones and cylinders.
static uint32_t aimdSideLineStep(uint32_t segments) {
    return std::max<uint32_t>(1, segments / 8);
}

static void aimdEmitCone(aimdContext* ctx, const aimdStyle& style, aimdVec3 apex, aimdVec3 baseCenter, float radius, uint32_t color) {
    aimdVec3 u, v;
    aimdBasis(apex - baseCenter, u, v);
    uint32_t segments = aimdSegmentCount(style);

    if (aimdIsFilled(style)) {
        aimdVec3 axis = apex - baseCenter;
        float height = aimdLength(axis);
        axis = aimdNormalize(axis);
        // Side normal: tilt the radial direction toward the apex by the cone's slope.
        auto sideNormal = [&](float i) { return aimdNormalize(aimdCircleDir(u, v, i, segments) * height + axis * radius); };

        aimdVec3 prev = aimdCirclePoint(baseCenter, u, v, radius, 0, segments);
        for (uint32_t i = 1; i <= segments; ++i) {
            aimdVec3 next = aimdCirclePoint(baseCenter, u, v, radius, i, segments);
            aimdEmitTriangleN(ctx, style, apex, prev, next, sideNormal((float)i - 0.5f), sideNormal((float)(i - 1)), sideNormal((float)i), color, true);
            prev = next;
        }
        aimdVec3 down = axis * -1.0f;
        aimdEmitDisc(ctx, style, baseCenter, u, v, radius, color, &down);
    } else {
        aimdEmitCircleOutline(ctx, style, baseCenter, u, v, radius, color);
        for (uint32_t i = 0; i < segments; i += aimdSideLineStep(segments))
            aimdEmitLine(ctx, style, apex, aimdCirclePoint(baseCenter, u, v, radius, i, segments), color);
    }
}

static void aimdEmitArrow(aimdContext* ctx, const aimdStyle& style, aimdVec3 from, aimdVec3 to, float headSize, uint32_t color) {
    aimdVec3 dir = to - from;
    float len = aimdLength(dir);
    if (len < 1e-6f)
        return;
    dir = dir * (1.0f / len);
    float head = std::min(headSize, len);
    aimdVec3 headBase = to - dir * head;

    // The shaft is always a line: a filled arrow only fills its head.
    if (len > head)
        aimdEmitLine(ctx, style, from, headBase, color);

    aimdStyle headStyle = style;
    headStyle.segments = std::min<uint32_t>(aimdSegmentCount(style), 16);
    aimdEmitCone(ctx, headStyle, to, headBase, head * 0.35f, color);
}

//
// Public shapes
//

#define AIMD_SHAPE_PROLOGUE()             \
    aimdContext* ctx = aimdCurrent();     \
    if (!ctx)                             \
        return;                           \
    const aimdStyle& style = ctx->styleStack.back()

void aimdBox(const float transform[16]) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        aimdVec3 local = { (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f };
        corners[i] = aimdTransformPoint(transform, local);
    }
    aimdEmitHexahedron(ctx, style, corners);
}

void aimdAABB(aimdVec3 min, aimdVec3 max) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = { (i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z };
    aimdEmitHexahedron(ctx, style, corners);
}

void aimdFrustum(const float m[16]) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? 1.0f : -1.0f;
        float y = (i & 2) ? 1.0f : -1.0f;
        float z = (i & 4) ? 1.0f : 0.0f;
        float w = m[3] * x + m[7] * y + m[11] * z + m[15];
        // A plane at infinity (infinite projection) has no finite corners to draw.
        if (std::fabs(w) < 1e-7f)
            return;
        corners[i] = aimdTransformPoint(m, { x, y, z }) * (1.0f / w);
    }
    aimdEmitHexahedron(ctx, style, corners);
}

void aimdCone(aimdVec3 apex, aimdVec3 baseCenter, float radius) {
    AIMD_SHAPE_PROLOGUE();
    aimdEmitCone(ctx, style, apex, baseCenter, radius, style.color);
}

void aimdCylinder(aimdVec3 a, aimdVec3 b, float radius) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 u, v;
    aimdBasis(b - a, u, v);
    uint32_t segments = aimdSegmentCount(style);

    if (aimdIsFilled(style)) {
        aimdVec3 axis = aimdNormalize(b - a);
        aimdVec3 down = axis * -1.0f;
        aimdEmitDisc(ctx, style, a, u, v, radius, style.color, &down);
        aimdEmitDisc(ctx, style, b, u, v, radius, style.color, &axis);
        aimdVec3 prevA = aimdCirclePoint(a, u, v, radius, 0, segments);
        aimdVec3 prevB = aimdCirclePoint(b, u, v, radius, 0, segments);
        aimdVec3 prevN = aimdCircleDir(u, v, 0.0f, segments);
        for (uint32_t i = 1; i <= segments; ++i) {
            aimdVec3 nextA = aimdCirclePoint(a, u, v, radius, i, segments);
            aimdVec3 nextB = aimdCirclePoint(b, u, v, radius, i, segments);
            aimdVec3 nextN = aimdCircleDir(u, v, (float)i, segments);
            aimdEmitTriangleN(ctx, style, prevA, nextA, nextB, prevN, nextN, nextN, style.color, true);
            aimdEmitTriangleN(ctx, style, prevA, nextB, prevB, prevN, nextN, prevN, style.color, true);
            prevA = nextA;
            prevB = nextB;
            prevN = nextN;
        }
    } else {
        aimdEmitCircleOutline(ctx, style, a, u, v, radius, style.color);
        aimdEmitCircleOutline(ctx, style, b, u, v, radius, style.color);
        for (uint32_t i = 0; i < segments; i += aimdSideLineStep(segments))
            aimdEmitLine(ctx, style, aimdCirclePoint(a, u, v, radius, i, segments), aimdCirclePoint(b, u, v, radius, i, segments), style.color);
    }
}

void aimdArrow(aimdVec3 from, aimdVec3 to, float headSize) {
    AIMD_SHAPE_PROLOGUE();
    aimdEmitArrow(ctx, style, from, to, headSize, style.color);
}

void aimdAxes(const float transform[16], float size) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 origin = { transform[12], transform[13], transform[14] };
    aimdVec3 axes[3] = {
        aimdNormalize({ transform[0], transform[1], transform[2] }),
        aimdNormalize({ transform[4], transform[5], transform[6] }),
        aimdNormalize({ transform[8], transform[9], transform[10] }),
    };
    const uint32_t colors[3] = { AIMD_COLOR_X, AIMD_COLOR_Y, AIMD_COLOR_Z };
    for (int i = 0; i < 3; ++i)
        aimdEmitArrow(ctx, style, origin, origin + axes[i] * size, size * 0.2f, colors[i]);
}

void aimdCircle(aimdVec3 center, aimdVec3 normal, float radius) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 u, v;
    aimdBasis(normal, u, v);
    if (aimdIsFilled(style))
        aimdEmitDisc(ctx, style, center, u, v, radius, style.color);
    else
        aimdEmitCircleOutline(ctx, style, center, u, v, radius, style.color);
}

void aimdRing(aimdVec3 center, aimdVec3 normal, float innerRadius, float outerRadius) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 u, v;
    aimdBasis(normal, u, v);
    uint32_t segments = aimdSegmentCount(style);

    if (aimdIsFilled(style)) {
        aimdVec3 prevIn = aimdCirclePoint(center, u, v, innerRadius, 0, segments);
        aimdVec3 prevOut = aimdCirclePoint(center, u, v, outerRadius, 0, segments);
        for (uint32_t i = 1; i <= segments; ++i) {
            aimdVec3 nextIn = aimdCirclePoint(center, u, v, innerRadius, i, segments);
            aimdVec3 nextOut = aimdCirclePoint(center, u, v, outerRadius, i, segments);
            aimdEmitTriangle(ctx, style, prevIn, prevOut, nextOut, style.color);
            aimdEmitTriangle(ctx, style, prevIn, nextOut, nextIn, style.color);
            prevIn = nextIn;
            prevOut = nextOut;
        }
    } else {
        aimdEmitCircleOutline(ctx, style, center, u, v, innerRadius, style.color);
        aimdEmitCircleOutline(ctx, style, center, u, v, outerRadius, style.color);
    }
}

void aimdRings(aimdVec3 center, float radius) {
    AIMD_SHAPE_PROLOGUE();
    const aimdVec3 x = { 1.0f, 0.0f, 0.0f }, y = { 0.0f, 1.0f, 0.0f }, z = { 0.0f, 0.0f, 1.0f };
    // Each ring is colored after the axis it rotates around.
    aimdEmitCircleOutline(ctx, style, center, y, z, radius, AIMD_COLOR_X);
    aimdEmitCircleOutline(ctx, style, center, z, x, radius, AIMD_COLOR_Y);
    aimdEmitCircleOutline(ctx, style, center, x, y, radius, AIMD_COLOR_Z);
}

void aimdSphere(aimdVec3 center, float radius) {
    AIMD_SHAPE_PROLOGUE();
    uint32_t segments = aimdSegmentCount(style);
    aimdSphereEx(center, radius, std::max<uint32_t>(2, segments / 2), segments);
}

void aimdSphereEx(aimdVec3 center, float radius, uint32_t rings, uint32_t sectors) {
    AIMD_SHAPE_PROLOGUE();
    rings = std::clamp<uint32_t>(rings, 2, 256);
    sectors = std::clamp<uint32_t>(sectors, 3, 256);
    const aimdVec3 x = { 1.0f, 0.0f, 0.0f }, y = { 0.0f, 1.0f, 0.0f }, z = { 0.0f, 0.0f, 1.0f };

    // Unit direction at latitude index 'ring' (0 = north pole .. rings = south pole) and longitude index 'sector'.
    auto sphereDir = [&](uint32_t ring, uint32_t sector) {
        float theta = AIMD_PI * (float)ring / (float)rings;
        float phi = 2.0f * AIMD_PI * (float)sector / (float)sectors;
        return y * std::cos(theta) + (x * std::cos(phi) + z * std::sin(phi)) * std::sin(theta);
    };

    if (aimdIsFilled(style)) {
        for (uint32_t r = 0; r < rings; ++r) {
            for (uint32_t s = 0; s < sectors; ++s) {
                aimdVec3 na = sphereDir(r, s), nb = sphereDir(r, s + 1);
                aimdVec3 nc = sphereDir(r + 1, s + 1), nd = sphereDir(r + 1, s);
                aimdVec3 a = center + na * radius, b = center + nb * radius;
                aimdVec3 c = center + nc * radius, d = center + nd * radius;
                // The pole rows collapse to a single triangle.
                if (r != 0)
                    aimdEmitTriangleN(ctx, style, a, b, c, na, nb, nc, style.color, true);
                if (r != rings - 1)
                    aimdEmitTriangleN(ctx, style, a, c, d, na, nc, nd, style.color, true);
            }
        }
    } else {
        // Exactly the edges of the filled tessellation: latitude circles, then pole-to-pole meridians.
        for (uint32_t r = 1; r < rings; ++r)
            for (uint32_t s = 0; s < sectors; ++s)
                aimdEmitLine(ctx, style, center + sphereDir(r, s) * radius, center + sphereDir(r, s + 1) * radius, style.color);
        for (uint32_t s = 0; s < sectors; ++s)
            for (uint32_t r = 0; r < rings; ++r)
                aimdEmitLine(ctx, style, center + sphereDir(r, s) * radius, center + sphereDir(r + 1, s) * radius, style.color);
    }
}

void aimdGrid(aimdVec3 center, aimdVec3 normal, float size, uint32_t cells) {
    AIMD_SHAPE_PROLOGUE();
    if (cells == 0)
        return;
    aimdVec3 u, v;
    aimdBasis(normal, u, v);
    float half = size * 0.5f;
    float step = size / (float)cells;
    for (uint32_t i = 0; i <= cells; ++i) {
        float offset = -half + step * (float)i;
        aimdEmitLine(ctx, style, center + u * offset - v * half, center + u * offset + v * half, style.color);
        aimdEmitLine(ctx, style, center + v * offset - u * half, center + v * offset + u * half, style.color);
    }
}

#undef AIMD_SHAPE_PROLOGUE

#endif // AIMD_IMPLEMENTATION
