/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#include "Showcase.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

static const char* kShapeNames[SHAPE_COUNT] = {
    "Box", "Frustum", "Cone", "Cylinder", "Arrow", "Axes", "Ring", "Rings", "Sphere", "Points", "Polyline",
};

static aimdVec3 V(const glm::vec3& v) {
    return { v.x, v.y, v.z };
}

static uint32_t ToColor(const float c[4]) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
}

ShowcaseSettings::ShowcaseSettings() {
    static const float kPalette[SHAPE_COUNT][3] = {
        { 1.00f, 0.60f, 0.20f }, { 0.95f, 0.95f, 0.40f }, { 0.40f, 0.85f, 1.00f }, { 0.60f, 1.00f, 0.55f },
        { 1.00f, 0.45f, 0.55f }, { 1.00f, 1.00f, 1.00f }, { 0.75f, 0.55f, 1.00f }, { 1.00f, 1.00f, 1.00f },
        { 0.35f, 0.70f, 1.00f }, { 1.00f, 0.85f, 0.30f }, { 0.40f, 1.00f, 0.85f },
    };
    for (int i = 0; i < SHAPE_COUNT; ++i) {
        shapes[i] = { { kPalette[i][0], kPalette[i][1], kPalette[i][2], 1.0f }, false, false, true };
    }
    shapes[SHAPE_BOX].filled = true;
    shapes[SHAPE_BOX].color[3] = 0.6f;
    shapes[SHAPE_SPHERE].filled = true;
    shapes[SHAPE_SPHERE].color[3] = 0.5f;
    shapes[SHAPE_RING].filled = true;
}

// Two rows of shapes: the first 6 at z = -3, the rest at z = +3.
static glm::vec3 SlotPosition(int shape) {
    const int perRow = 6;
    int row = shape / perRow;
    int column = shape % perRow;
    return glm::vec3(-7.5f + 3.0f * (float)column, 0.0f, row == 0 ? -3.0f : 3.0f);
}

static aimdStyle ShapeStyle(const ShowcaseSettings& settings, const ShowcaseShapeSettings& shape) {
    aimdStyle style = aimdDefaultStyle();
    style.color = ToColor(shape.color);
    style.thickness = settings.thickness;
    style.pointSize = settings.pointSize;
    style.segments = (uint32_t)settings.segments;
    style.flags = AIMD_STYLE_NONE;
    if (shape.filled)
        style.flags |= AIMD_STYLE_FILLED;
    if (shape.xray)
        style.flags |= AIMD_STYLE_NO_DEPTH_TEST;
    if (settings.roundPoints)
        style.flags |= AIMD_STYLE_ROUND_POINTS;
    if (settings.shaded)
        style.flags |= AIMD_STYLE_SHADED;
    return style;
}

static void DrawShape(const ShowcaseSettings& settings, int shape, glm::vec3 slot, float t) {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 center = slot + glm::vec3(0.0f, 1.0f, 0.0f);

    switch (shape) {
    case SHAPE_BOX: {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), center);
        m = glm::rotate(m, t * 0.7f, glm::normalize(glm::vec3(1.0f, 1.0f, 0.3f)));
        m = glm::scale(m, glm::vec3(1.3f, 0.8f, 1.0f));
        aimdBox(glm::value_ptr(m));
        break;
    }
    case SHAPE_FRUSTUM: {
        // A small camera spinning in place; its frustum is what gets drawn.
        glm::vec3 eye = center + glm::vec3(0.0f, 0.2f, 0.0f);
        glm::vec3 dir(std::cos(t * 0.6f), -0.3f, std::sin(t * 0.6f));
        glm::mat4 view = glm::lookAtRH(eye, eye + dir, up);
        glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.0f), 16.0f / 9.0f, 0.15f, 1.3f);
        glm::mat4 inv = glm::inverse(proj * view);
        aimdFrustum(glm::value_ptr(inv));
        aimdPoint(V(eye));
        break;
    }
    case SHAPE_CONE: {
        glm::vec3 tilt(std::sin(t) * 0.3f, 0.0f, std::cos(t * 0.8f) * 0.3f);
        aimdCone(V(slot + glm::vec3(0.0f, 2.0f, 0.0f) + tilt), V(slot + glm::vec3(0.0f, 0.2f, 0.0f)), 0.7f);
        break;
    }
    case SHAPE_CYLINDER: {
        glm::vec3 top = slot + glm::vec3(std::sin(t) * 0.4f, 1.9f, 0.0f);
        aimdCylinder(V(slot + glm::vec3(0.0f, 0.2f, 0.0f)), V(top), 0.5f);
        break;
    }
    case SHAPE_ARROW: {
        glm::vec3 from = slot + glm::vec3(0.0f, 0.2f, 0.0f);
        glm::vec3 to = center + glm::vec3(std::cos(t) * 0.9f, 0.6f + std::sin(t * 1.3f) * 0.3f, std::sin(t) * 0.9f);
        aimdArrow(V(from), V(to), 0.45f);
        break;
    }
    case SHAPE_AXES: {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), center);
        m = glm::rotate(m, t * 0.5f, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        aimdAxes(glm::value_ptr(m), 1.1f);
        break;
    }
    case SHAPE_RING: {
        glm::vec3 normal(std::sin(t * 0.7f), 1.0f, std::cos(t * 0.5f) * 0.6f);
        aimdRing(V(center), V(normal), 0.5f, 0.85f);
        break;
    }
    case SHAPE_RINGS:
        aimdRings(V(center), 0.85f + std::sin(t * 2.0f) * 0.05f);
        break;
    case SHAPE_SPHERE:
        aimdSphereEx(V(center), 0.8f + std::sin(t * 1.5f) * 0.1f, (uint32_t)settings.sphereRings, (uint32_t)settings.sphereSectors);
        break;
    case SHAPE_POINTS: {
        // A jittering 4x4x4 point cloud with per-point colors.
        aimdStyle* style = aimdGetStyle();
        uint32_t baseAlpha = style->color >> 24;
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    glm::vec3 local = glm::vec3((float)x, (float)y, (float)z) / 3.0f - 0.5f;
                    float wobble = std::sin(t * 2.0f + (float)(x + y * 4 + z * 16)) * 0.05f;
                    aimdSetColor(AIMD_RGBA(64 + x * 60, 64 + y * 60, 64 + z * 60, baseAlpha));
                    aimdPoint(V(center + local * 1.6f + glm::vec3(wobble)));
                }
        break;
    }
    case SHAPE_POLYLINE: {
        aimdVec3 helix[96];
        for (int i = 0; i < 96; ++i) {
            float a = (float)i / 95.0f;
            float angle = a * 6.0f * glm::pi<float>() + t;
            float radius = 0.3f + 0.5f * a;
            helix[i] = V(slot + glm::vec3(std::cos(angle) * radius, 0.2f + a * 1.8f, std::sin(angle) * radius));
        }
        aimdPolyline(helix, 96, 0);
        break;
    }
    default:
        break;
    }
}

void ShowcaseDrawScene(const ShowcaseSettings& settings, float time) {
    float t = settings.animate ? time * settings.animationSpeed : 0.0f;

    if (settings.showGrid) {
        aimdStyle grid = aimdDefaultStyle();
        grid.color = AIMD_RGBA(128, 128, 140, 110);
        grid.thickness = 1.0f;
        aimdPushStyle(&grid);
        aimdGrid({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 40.0f, 40);
        aimdPopStyle();
    }

    for (int i = 0; i < SHAPE_COUNT; ++i) {
        const ShowcaseShapeSettings& shape = settings.shapes[i];
        if (!shape.visible)
            continue;
        glm::vec3 slot = SlotPosition(i);

        aimdStyle style = ShapeStyle(settings, shape);
        aimdPushStyle(&style);
        DrawShape(settings, i, slot, t);
        aimdPopStyle();

        if (settings.showLabels) {
            aimdStyle text = aimdDefaultStyle();
            text.color = AIMD_RGBA(255, 255, 255, 255);
            text.textSize = settings.textPixelSize ? settings.textPixelHeight : settings.textWorldSize;
            text.flags = settings.textPixelSize ? AIMD_STYLE_TEXT_PIXEL_SIZE : AIMD_STYLE_NONE;
            aimdPushStyle(&text);
            aimdText(V(slot + glm::vec3(0.0f, 2.6f, 0.0f)), kShapeNames[i]);
            aimdPopStyle();
        }
    }

    // Stress test: a ring of spheres far out, to push the upload buffers past their initial size.
    if (settings.stressSpheres > 0) {
        aimdStyle style = ShapeStyle(settings, settings.shapes[SHAPE_SPHERE]);
        aimdPushStyle(&style);
        for (int i = 0; i < settings.stressSpheres; ++i) {
            float a = (float)i / (float)settings.stressSpheres * glm::two_pi<float>();
            float radius = 16.0f + (float)(i % 5) * 1.5f;
            aimdSphereEx({ std::cos(a + t * 0.1f) * radius, 1.0f + (float)(i % 3), std::sin(a + t * 0.1f) * radius }, 0.5f,
                         (uint32_t)settings.sphereRings, (uint32_t)settings.sphereSectors);
        }
        aimdPopStyle();
    }
}

void ShowcaseDrawUI(ShowcaseSettings& settings, const aimdStats& stats, float frameTime) {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("AIMD Style");

    ImGui::Text("%.2f ms (%.0f FPS)", frameTime * 1000.0f, frameTime > 0.0f ? 1.0f / frameTime : 0.0f);
    ImGui::Text("Lines %u  Points %u  Triangles %u", stats.lines, stats.points, stats.triangles);
    ImGui::Text("Labels %u  Draw calls %u", stats.texts, stats.drawCalls);
    ImGui::TextDisabled("Hold RMB to look, WASD + Q/E to move, Shift = fast");

    if (ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Line thickness (px)", &settings.thickness, 0.25f, 16.0f, "%.2f");
        ImGui::SliderFloat("Point size (px)", &settings.pointSize, 1.0f, 32.0f, "%.1f");
        ImGui::SliderInt("Segments", &settings.segments, 3, 128);
        ImGui::SliderInt("Sphere rings", &settings.sphereRings, 2, 64);
        ImGui::SliderInt("Sphere sectors", &settings.sphereSectors, 3, 128);
        ImGui::Checkbox("Round points", &settings.roundPoints);
        ImGui::SameLine();
        ImGui::Checkbox("Shaded fills", &settings.shaded);
        if (ImGui::Button("Fill all"))
            for (auto& shape : settings.shapes) shape.filled = true;
        ImGui::SameLine();
        if (ImGui::Button("Wireframe all"))
            for (auto& shape : settings.shapes) shape.filled = false;
        ImGui::SameLine();
        if (ImGui::Button("X-ray all"))
            for (auto& shape : settings.shapes) shape.xray = true;
        ImGui::SameLine();
        if (ImGui::Button("Depth all"))
            for (auto& shape : settings.shapes) shape.xray = false;
    }

    if (ImGui::CollapsingHeader("Shapes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("shapes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Color");
            ImGui::TableSetupColumn("Show");
            ImGui::TableSetupColumn("Filled");
            ImGui::TableSetupColumn("X-ray");
            ImGui::TableHeadersRow();
            for (int i = 0; i < SHAPE_COUNT; ++i) {
                ShowcaseShapeSettings& shape = settings.shapes[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kShapeNames[i]);
                ImGui::TableNextColumn();
                ImGui::ColorEdit4("##color", shape.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##visible", &shape.visible);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##filled", &shape.filled);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##xray", &shape.xray);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show labels", &settings.showLabels);
        ImGui::Checkbox("Constant pixel size", &settings.textPixelSize);
        if (settings.textPixelSize)
            ImGui::SliderFloat("Height (px)", &settings.textPixelHeight, 6.0f, 64.0f, "%.0f");
        else
            ImGui::SliderFloat("Height (world)", &settings.textWorldSize, 0.05f, 2.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("GPU driven", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##gpu", &settings.gpuEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Freeze culling", &settings.gpuFreezeCulling);
        ImGui::SameLine();
        ImGui::Checkbox("Show culled", &settings.gpuShowCulled);
        ImGui::Checkbox("Filled##gpu", &settings.gpuFilled);
        ImGui::SameLine();
        ImGui::Checkbox("Spheres##gpu", &settings.gpuSpheres);
        ImGui::SameLine();
        ImGui::Checkbox("X-ray##gpu", &settings.gpuXray);
        ImGui::SliderInt("Instances", &settings.gpuInstances, 0, 100000, "%d", ImGuiSliderFlags_Logarithmic);
        ImGui::Text("GPU requested: lines %u  points %u  triangles %u", stats.gpuLines, stats.gpuPoints, stats.gpuTriangles);
        if (stats.gpuOverflow)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Overflow: shapes dropped, growing buffers");
        else
            ImGui::TextDisabled("All GPU shapes fit");
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Depth target", &settings.useDepth);
        ImGui::SameLine();
        ImGui::Checkbox("Depth write", &settings.depthWrite);
        ImGui::SameLine();
        ImGui::Checkbox("Reverse-Z", &settings.reverseZ);
        ImGui::Checkbox("Grid", &settings.showGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Animate", &settings.animate);
        ImGui::SliderFloat("Speed", &settings.animationSpeed, 0.0f, 4.0f, "%.2f");
        ImGui::SliderInt("Stress spheres", &settings.stressSpheres, 0, 2000);
    }

    ImGui::End();
}
