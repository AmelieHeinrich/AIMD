/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * Demo of AIMD's GPU-driven path: a fake instance-culling pass. Each thread animates one instance, frustum-culls its
 * bounding sphere against a (possibly frozen) culling camera, and draws the result with AIMDDebugRenderer.
 * Entry point: SceneCS.
 */

#include "Shaders/AIMDDebug.hlsli"

#if defined(AGFX_VULKAN)
    #define SCENE_PUSH_CONSTANTS(type, name) [[vk::push_constant]] ConstantBuffer<type> name : register(b0)
#else
    #define SCENE_PUSH_CONSTANTS(type, name) ConstantBuffer<type> name : register(b0)
#endif

static const uint SCENE_SHOW_CULLED = 1u << 0;
static const uint SCENE_FILLED = 1u << 1;
static const uint SCENE_SHADED = 1u << 2;
static const uint SCENE_SPHERES = 1u << 3;
static const uint SCENE_XRAY = 1u << 4;

// Must match SceneConstants in Demo/main.cpp
struct SceneConstants {
    column_major float4x4 cullViewProjection; // Finite, standard-Z projection
    uint aimdHandle;
    uint instanceCount;
    float time;
    uint flags;
    uint sphereRings;
    uint sphereSectors;
    float thickness;
    uint pad;
};
SCENE_PUSH_CONSTANTS(SceneConstants, Scene);

float Hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)(x & 0xFFFFFFu) / 16777216.0;
}

float3 Hue(float h) {
    return saturate(abs(frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}

bool SphereInFrustum(float4x4 m, float3 center, float radius) {
    // Planes from the rows of the view-projection (clip-space depth in [0, 1]).
    float4 planes[6] = {
        m[3] + m[0], m[3] - m[0],
        m[3] + m[1], m[3] - m[1],
        m[2],        m[3] - m[2],
    };
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius * length(planes[i].xyz))
            return false;
    }
    return true;
}

[numthreads(64, 1, 1)]
void SceneCS(uint id : SV_DispatchThreadID) {
    if (id >= Scene.instanceCount)
        return;

    // Instances on a grid filling a fixed field behind the showcase.
    const float2 fieldSize = float2(80.0, 60.0);
    uint side = (uint)ceil(sqrt((float)Scene.instanceCount));
    float2 cell = fieldSize / (float)side;
    float h = Hash(id);
    float3 position = float3(-fieldSize.x * 0.5 + ((float)(id % side) + 0.5) * cell.x,
                             0.8 + sin(Scene.time * 1.5 + h * 6.2831) * 0.4,
                             -12.0 - ((float)(id / side) + 0.5) * cell.y);
    float size = min(cell.x, cell.y) * (0.2 + 0.12 * h);

    AIMDDebugRenderer renderer = AIMDDebugRenderer::Create(Scene.aimdHandle);
    renderer.SetThickness(Scene.thickness);
    renderer.SetDepthTest((Scene.flags & SCENE_XRAY) == 0);

    bool visible = SphereInFrustum(Scene.cullViewProjection, position, size * 1.7321);
    if (visible) {
        renderer.SetColor(float4(Hue(h), 1.0));
        renderer.SetFilled((Scene.flags & SCENE_FILLED) != 0);
        renderer.SetShaded((Scene.flags & SCENE_SHADED) != 0);

        if (Scene.flags & SCENE_SPHERES) {
            renderer.DrawSphereEx(position, size, Scene.sphereRings, Scene.sphereSectors);
        } else {
            float angle = Scene.time * (0.5 + h);
            float c = cos(angle) * size * 2.0;
            float s = sin(angle) * size * 2.0;
            float4x4 transform = float4x4(c,    0.0,        s,   position.x,
                                          0.0,  size * 2.0, 0.0, position.y,
                                          -s,   0.0,        c,   position.z,
                                          0.0,  0.0,        0.0, 1.0);
            renderer.DrawBox(transform);
        }

        renderer.SetPointSize(4.0);
        renderer.SetRoundPoints(true);
        renderer.DrawPoint(position + float3(0.0, size * 1.8, 0.0));
    } else if (Scene.flags & SCENE_SHOW_CULLED) {
        renderer.SetColor(float4(1.0, 0.15, 0.1, 0.8));
        renderer.SetFilled(false);
        renderer.DrawAABB(position - size, position + size);
    }
}
