/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#include "aimd_internal.h"

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
