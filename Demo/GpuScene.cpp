/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#include "GpuScene.h"
#include "Platform.h"
#include "Showcase.h"

#include <glm/gtc/type_ptr.hpp>
#include <cstring>

// Must match SceneConstants in Demo/Shaders/GpuScene.hlsl
struct SceneConstants {
    float cullViewProjection[16];
    uint32_t aimdHandle;
    uint32_t instanceCount;
    float time;
    uint32_t flags;
    uint32_t sphereRings;
    uint32_t sphereSectors;
    float thickness;
    uint32_t pad;
};

enum SceneFlags : uint32_t {
    SCENE_SHOW_CULLED = 1 << 0,
    SCENE_FILLED = 1 << 1,
    SCENE_SHADED = 1 << 2,
    SCENE_SPHERES = 1 << 3,
    SCENE_XRAY = 1 << 4,
};

bool GpuScene::Init(Platform& platform) {
    mShader = platform.CompileShader("Demo/Shaders/GpuScene.hlsl", "SceneCS", AGFX_SHADER_STAGE_COMPUTE, mGroupSize);
    if (!mShader)
        return false;

    agfxComputePipelineCreateInfo info = {};
    info.name = "Demo GPU Scene";
    info.computeShader = mShader;
    info.groupSizeX = mGroupSize[0];
    info.groupSizeY = mGroupSize[1];
    info.groupSizeZ = mGroupSize[2];
    mPipeline = agfxComputePipelineCreate(platform.Device(), &info);
    return mPipeline != nullptr;
}

void GpuScene::Shutdown(agfxDevice* device) {
    if (mPipeline)
        agfxComputePipelineDestroy(device, mPipeline);
    if (mShader)
        agfxShaderModuleDestroy(device, mShader);
    mPipeline = nullptr;
    mShader = nullptr;
}

void GpuScene::Dispatch(agfxCommandBuffer* commandBuffer, uint32_t aimdHandle, const ShowcaseSettings& settings,
                        const glm::mat4& viewProjection, float time) {
    if (!settings.gpuEnabled || settings.gpuInstances <= 0)
        return;

    SceneConstants constants = {};
    memcpy(constants.cullViewProjection, glm::value_ptr(viewProjection), sizeof(constants.cullViewProjection));
    constants.aimdHandle = aimdHandle;
    constants.instanceCount = (uint32_t)settings.gpuInstances;
    constants.time = settings.animate ? time * settings.animationSpeed : 0.0f;
    constants.flags = (settings.gpuShowCulled ? SCENE_SHOW_CULLED : 0) | (settings.gpuFilled ? SCENE_FILLED : 0) |
                      (settings.shaded ? SCENE_SHADED : 0) | (settings.gpuSpheres ? SCENE_SPHERES : 0) |
                      (settings.gpuXray ? SCENE_XRAY : 0);
    constants.sphereRings = (uint32_t)settings.sphereRings;
    constants.sphereSectors = (uint32_t)settings.sphereSectors;
    constants.thickness = settings.thickness;

    agfxComputePass* pass = agfxComputePassBegin(commandBuffer, "Demo GPU Scene");
    agfxComputePassSetPipeline(pass, mPipeline);
    agfxComputePassPushConstants(pass, &constants, sizeof(constants));
    agfxComputePassDispatch(pass, (constants.instanceCount + mGroupSize[0] - 1) / mGroupSize[0], 1, 1);
    agfxComputePassEnd(pass);
}
