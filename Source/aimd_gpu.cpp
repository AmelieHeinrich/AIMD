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

#include "aimd_internal.h"

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
