/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * The demo scene: one labeled, animated instance of every AIMD shape, and the ImGui panel that styles them.
 */

#pragma once

#include <aimd/aimd.h>
#include <stdint.h>

enum ShowcaseShape {
    SHAPE_BOX,
    SHAPE_FRUSTUM,
    SHAPE_CONE,
    SHAPE_CYLINDER,
    SHAPE_ARROW,
    SHAPE_AXES,
    SHAPE_RING,
    SHAPE_RINGS,
    SHAPE_SPHERE,
    SHAPE_POINTS,
    SHAPE_POLYLINE,
    SHAPE_COUNT
};

struct ShowcaseShapeSettings {
    float color[4];
    bool filled;
    bool xray; // AIMD_STYLE_NO_DEPTH_TEST
    bool visible;
};

struct ShowcaseSettings {
    ShowcaseShapeSettings shapes[SHAPE_COUNT];

    float thickness = 2.0f;
    float pointSize = 8.0f;
    int segments = 24;
    bool roundPoints = true;
    bool shaded = true;
    int sphereRings = 8;
    int sphereSectors = 16;

    bool showLabels = true;
    bool textPixelSize = false;
    float textWorldSize = 0.3f;
    float textPixelHeight = 18.0f;

    bool showGrid = true;
    bool animate = true;
    float animationSpeed = 1.0f;
    int stressSpheres = 0;

    // GPU-driven scene (Shaders/DemoGpuScene.hlsl)
    bool gpuEnabled = true;
    int gpuInstances = 4096;
    bool gpuFreezeCulling = false;
    bool gpuShowCulled = true;
    bool gpuFilled = true;
    bool gpuSpheres = false;
    bool gpuXray = false;

    // Execute-level options
    bool useDepth = true;
    bool depthWrite = false;
    bool reverseZ = true;

    ShowcaseSettings();
};

void ShowcaseDrawUI(ShowcaseSettings& settings, const aimdStats& stats, float frameTime);
void ShowcaseDrawScene(const ShowcaseSettings& settings, float time);
