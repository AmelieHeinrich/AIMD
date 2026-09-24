/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#pragma once

#include <aimd/aimd.h>

#include <vector>
#include <string>

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
