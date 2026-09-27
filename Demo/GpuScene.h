/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * Demo of AIMD's GPU-driven path: a compute pass (Demo/Shaders/GpuScene.hlsl) standing in for an instance-culling pass,
 * which draws every instance, culled or not, with AIMDDebugRenderer.
 */

#pragma once

#include <agfx/agfx.h>
#include <glm/glm.hpp>
#include <stdint.h>

class Platform;
struct ShowcaseSettings;

class GpuScene {
public:
    bool Init(Platform& platform);
    void Shutdown(agfxDevice* device);

    /// @brief Records the scene's compute pass. Call between aimdGpuBeginFrame (which returned aimdHandle) and
    ///        aimdExecute, on the same command buffer.
    /// @param viewProjection Culling camera. It must be finite and standard-Z so the shader can extract all six planes.
    void Dispatch(agfxCommandBuffer* commandBuffer, uint32_t aimdHandle, const ShowcaseSettings& settings,
                  const glm::mat4& viewProjection, float time);

private:
    agfxShaderModule* mShader = nullptr;
    agfxComputePipeline* mPipeline = nullptr;
    uint32_t mGroupSize[3] = {};
};
