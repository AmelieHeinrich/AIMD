/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * AIMD demo. This file holds every AIMD integration step; the rest of the demo is scenery:
 *   Platform.*   window, agfx device, swap chain, ImGui. No AIMD in there.
 *   Showcase.*   the CPU-side shapes (aimd* calls) and the ImGui panel that styles them.
 *   GpuScene.*   a compute pass drawing shapes through the GPU-driven path (AIMDDebug.hlsli).
 *
 * AIMD itself comes from the single-header build in dist/, compiled in AimdImplementation.cpp.
 */

#include "Camera.h"
#include "GpuScene.h"
#include "Platform.h"
#include "Showcase.h"

#include <aimd.h>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <cfloat>
#include <cstdio>
#include <cstring>

// AIMD doesn't rasterize text: aimdExecute projects the labels and hands them to this callback, which draws them with
// ImGui. AIMD works in framebuffer pixels, ImGui in window points.
static void DrawLabels(const aimdTextCommand* commands, uint32_t count, void*) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float invScale = scale.x > 0.0f ? 1.0f / scale.x : 1.0f;

    for (uint32_t i = 0; i < count; ++i) {
        const aimdTextCommand& cmd = commands[i];
        float size = cmd.pixelHeight * invScale;
        ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, cmd.text);
        ImVec2 pos(cmd.x * invScale - extent.x * 0.5f, cmd.y * invScale - extent.y * 0.5f);
        uint32_t shadowAlpha = (cmd.color >> 24) * 3 / 4;
        drawList->AddText(font, size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, shadowAlpha), cmd.text);
        drawList->AddText(font, size, pos, cmd.color, cmd.text);
    }
}

static aimdContext* CreateAimd(Platform& platform) {
    aimdContextCreateInfo info = {};
    info.device = platform.Device();
    info.framesInFlight = Platform::kFramesInFlight;
    info.lineVertexShader = platform.CompileShader("dist/AIMD.hlsl", "LineVS", AGFX_SHADER_STAGE_VERTEX);
    info.pointVertexShader = platform.CompileShader("dist/AIMD.hlsl", "PointVS", AGFX_SHADER_STAGE_VERTEX);
    info.triangleVertexShader = platform.CompileShader("dist/AIMD.hlsl", "TriangleVS", AGFX_SHADER_STAGE_VERTEX);
    info.fragmentShader = platform.CompileShader("dist/AIMD.hlsl", "MainPS", AGFX_SHADER_STAGE_FRAGMENT);

    // GPU-driven path
    info.enableGpu = 1;
    info.finalizeComputeShader = platform.CompileShader("dist/AIMD.hlsl", "FinalizeCS", AGFX_SHADER_STAGE_COMPUTE);
    info.gpuInitialCapacity = 1024; // Deliberately small, to show auto-grow
    info.gpuAutoGrow = 1;
    return aimdContextCreate(&info);
}

// Stands in for the application's own rendering, which AIMD draws on top of.
static void RenderScene(Platform& platform, const PlatformFrame& frame, bool reverseZ) {
    agfxRenderPassCreateInfo passInfo = {};
    passInfo.name = "Clear";
    passInfo.width = platform.Width();
    passInfo.height = platform.Height();
    passInfo.colorAttachmentCount = 1;
    passInfo.colorAttachments[0].renderTarget = frame.colorTarget;
    passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_CLEAR;
    passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
    passInfo.colorAttachments[0].clearColor[0] = 0.08f;
    passInfo.colorAttachments[0].clearColor[1] = 0.085f;
    passInfo.colorAttachments[0].clearColor[2] = 0.1f;
    passInfo.colorAttachments[0].clearColor[3] = 1.0f;
    passInfo.hasDepthAttachment = 1;
    passInfo.depthAttachment.renderTarget = platform.DepthTarget();
    passInfo.depthAttachment.loadOp = AGFX_LOAD_OPERATION_CLEAR;
    passInfo.depthAttachment.storeOp = AGFX_STORE_OPERATION_STORE;
    passInfo.depthAttachment.clearDepth = reverseZ ? 0.0f : 1.0f;
    agfxRenderPassEnd(agfxRenderPassBegin(frame.commandBuffer, &passInfo));
}

static void Run(Platform& platform, GpuScene& gpuScene) {
    Camera camera;
    ShowcaseSettings settings;
    glm::mat4 frozenCullViewProjection(1.0f);

    PlatformInput input;
    while (platform.PollEvents(input)) {
        PlatformFrame frame;
        if (!platform.BeginFrame(frame))
            continue;

        camera.Look(input.lookX, input.lookY);
        camera.Move(input.forward, input.right, input.up, frame.deltaTime, input.fast);
        float aspect = (float)platform.Width() / (float)platform.Height();
        glm::mat4 view = camera.View();
        glm::mat4 projection = camera.Projection(aspect, settings.reverseZ);

        // 1. CPU shapes: aimd* calls, from anywhere, at any point before aimdExecute.
        aimdStats stats;
        aimdGetStats(&stats);
        ShowcaseDrawUI(settings, stats, frame.deltaTime);
        ShowcaseDrawScene(settings, frame.time);

        glm::mat4 cullViewProjection = glm::perspectiveRH_ZO(camera.fovY, aspect, 0.1f, 90.0f) * view;
        if (settings.gpuFreezeCulling) {
            cullViewProjection = frozenCullViewProjection;
            aimdStyle style = aimdDefaultStyle();
            style.color = AIMD_RGBA(255, 220, 60, 255);
            aimdPushStyle(&style);
            aimdFrustum(glm::value_ptr(glm::inverse(cullViewProjection)));
            aimdPopStyle();
        } else {
            frozenCullViewProjection = cullViewProjection;
        }

        // 2. GPU shapes: start AIMD's GPU frame on the command buffer, then pass the handle to the compute shaders that
        //    draw. AIMD records the barriers they need.
        uint32_t aimdHandle = aimdGpuBeginFrame(frame.commandBuffer, frame.index);
        if (aimdHandle != AIMD_GPU_INVALID_HANDLE)
            gpuScene.Dispatch(frame.commandBuffer, aimdHandle, settings, cullViewProjection, frame.time);

        RenderScene(platform, frame, settings.reverseZ);

        // 3. Draw everything queued this frame, CPU and GPU, over the scene. Labels go to DrawLabels.
        aimdExecuteInfo executeInfo = {};
        executeInfo.commandBuffer = frame.commandBuffer;
        executeInfo.colorTarget = frame.colorTarget;
        executeInfo.colorFormat = platform.ColorFormat();
        executeInfo.depthTarget = settings.useDepth ? platform.DepthTarget() : nullptr;
        executeInfo.depthFormat = settings.useDepth ? Platform::kDepthFormat : AGFX_TEXTURE_FORMAT_UNKNOWN;
        executeInfo.width = platform.Width();
        executeInfo.height = platform.Height();
        memcpy(executeInfo.view, glm::value_ptr(view), sizeof(executeInfo.view));
        memcpy(executeInfo.projection, glm::value_ptr(projection), sizeof(executeInfo.projection));
        executeInfo.reverseZ = settings.reverseZ;
        executeInfo.depthWrite = settings.depthWrite;
        executeInfo.frameIndex = frame.index;
        executeInfo.textCallback = DrawLabels;
        aimdExecute(&executeInfo);

        platform.EndFrame(frame);
    }
}

int main(int, char**) {
    Platform platform;
    GpuScene gpuScene;
    aimdContext* aimd = nullptr;

    bool ok = platform.Init("AIMD Demo", 1600, 900);
    if (ok) {
        aimd = CreateAimd(platform);
        ok = aimd && gpuScene.Init(platform);
        if (!ok)
            fprintf(stderr, "Failed to create the AIMD context or the GPU scene\n");
    }
    if (ok)
        Run(platform, gpuScene);

    if (platform.Device()) {
        agfxDeviceWaitIdle(platform.Device());
        aimdContextDestroy(aimd);
        gpuScene.Shutdown(platform.Device());
    }
    platform.Shutdown();
    return ok ? 0 : 1;
}
