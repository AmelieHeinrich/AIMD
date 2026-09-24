/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * CPU tessellation of every AIMD shape into lines and triangles.
 */

#include "aimd_internal.h"

#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr uint32_t kColorX = AIMD_RGBA(235, 64, 52, 255);
static constexpr uint32_t kColorY = AIMD_RGBA(90, 200, 70, 255);
static constexpr uint32_t kColorZ = AIMD_RGBA(60, 120, 240, 255);

//
// Math helpers
//

static aimdVec3 operator+(aimdVec3 a, aimdVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static aimdVec3 operator-(aimdVec3 a, aimdVec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static aimdVec3 operator*(aimdVec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float dot(aimdVec3 a, aimdVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float length(aimdVec3 a) { return std::sqrt(dot(a, a)); }
static aimdVec3 cross(aimdVec3 a, aimdVec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static aimdVec3 normalize(aimdVec3 a) {
    float len = length(a);
    return len > 1e-12f ? a * (1.0f / len) : aimdVec3{ 0.0f, 1.0f, 0.0f };
}

// Column-major transform of a point, without the perspective divide.
static aimdVec3 transformPoint(const float m[16], aimdVec3 p) {
    return { m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
             m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
             m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] };
}

// Two unit vectors perpendicular to n and to each other.
static void basis(aimdVec3 n, aimdVec3& u, aimdVec3& v) {
    n = normalize(n);
    aimdVec3 helper = std::fabs(n.x) > 0.9f ? aimdVec3{ 0.0f, 1.0f, 0.0f } : aimdVec3{ 1.0f, 0.0f, 0.0f };
    u = normalize(cross(helper, n));
    v = cross(n, u);
}

static uint32_t segmentCount(const aimdStyle& style) {
    return std::clamp<uint32_t>(style.segments, 3, 256);
}

static bool isFilled(const aimdStyle& style) {
    return (style.flags & AIMD_STYLE_FILLED) != 0;
}

//
// Shared tessellation
//

// Corner i has x from bit 0, y from bit 1, z from bit 2.
static const int kBoxEdges[12][2] = {
    { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, // along x
    { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, // along y
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // along z
};
static const int kBoxFaces[6][4] = {
    { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, // -x, +x
    { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, // -y, +y
    { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, // -z, +z
};

static void emitHexahedron(aimdContext* ctx, const aimdStyle& style, const aimdVec3 corners[8]) {
    if (isFilled(style)) {
        aimdVec3 center = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 8; ++i)
            center = center + corners[i] * 0.125f;

        for (const auto& face : kBoxFaces) {
            const aimdVec3 &a = corners[face[0]], &b = corners[face[1]], &c = corners[face[2]], &d = corners[face[3]];
            // Orient the face normal away from the center, so any transform (even a mirroring one) works.
            aimdVec3 n = normalize(cross(c - a, b - a) + cross(d - a, c - a));
            if (dot(n, (a + c) * 0.5f - center) < 0.0f)
                n = n * -1.0f;
            aimdEmitTriangleN(ctx, style, a, b, c, n, n, n, style.color, true);
            aimdEmitTriangleN(ctx, style, a, c, d, n, n, n, style.color, true);
        }
    } else {
        for (const auto& edge : kBoxEdges)
            aimdEmitLine(ctx, style, corners[edge[0]], corners[edge[1]], style.color);
    }
}

// Unit direction of circle point i (may be fractional, for mid-segment normals).
static aimdVec3 circleDir(aimdVec3 u, aimdVec3 v, float i, uint32_t segments) {
    float angle = 2.0f * kPi * i / (float)segments;
    return u * std::cos(angle) + v * std::sin(angle);
}

static aimdVec3 circlePoint(aimdVec3 center, aimdVec3 u, aimdVec3 v, float radius, uint32_t i, uint32_t segments) {
    return center + circleDir(u, v, (float)i, segments) * radius;
}

static void emitCircleOutline(aimdContext* ctx, const aimdStyle& style, aimdVec3 center, aimdVec3 u, aimdVec3 v, float radius, uint32_t color) {
    uint32_t segments = segmentCount(style);
    aimdVec3 prev = circlePoint(center, u, v, radius, 0, segments);
    for (uint32_t i = 1; i <= segments; ++i) {
        aimdVec3 next = circlePoint(center, u, v, radius, i, segments);
        aimdEmitLine(ctx, style, prev, next, color);
        prev = next;
    }
}

// Two-sided disc, or one side of a closed shape when 'outward' is given.
static void emitDisc(aimdContext* ctx, const aimdStyle& style, aimdVec3 center, aimdVec3 u, aimdVec3 v, float radius, uint32_t color,
                     const aimdVec3* outward = nullptr) {
    uint32_t segments = segmentCount(style);
    aimdVec3 prev = circlePoint(center, u, v, radius, 0, segments);
    for (uint32_t i = 1; i <= segments; ++i) {
        aimdVec3 next = circlePoint(center, u, v, radius, i, segments);
        if (outward)
            aimdEmitTriangleN(ctx, style, center, prev, next, *outward, *outward, *outward, color, true);
        else
            aimdEmitTriangle(ctx, style, center, prev, next, color);
        prev = next;
    }
}

// Number of apex/side lines drawn on wireframe cones and cylinders.
static uint32_t sideLineStep(uint32_t segments) {
    return std::max<uint32_t>(1, segments / 8);
}

static void emitCone(aimdContext* ctx, const aimdStyle& style, aimdVec3 apex, aimdVec3 baseCenter, float radius, uint32_t color) {
    aimdVec3 u, v;
    basis(apex - baseCenter, u, v);
    uint32_t segments = segmentCount(style);

    if (isFilled(style)) {
        aimdVec3 axis = apex - baseCenter;
        float height = length(axis);
        axis = normalize(axis);
        // Side normal: tilt the radial direction toward the apex by the cone's slope.
        auto sideNormal = [&](float i) { return normalize(circleDir(u, v, i, segments) * height + axis * radius); };

        aimdVec3 prev = circlePoint(baseCenter, u, v, radius, 0, segments);
        for (uint32_t i = 1; i <= segments; ++i) {
            aimdVec3 next = circlePoint(baseCenter, u, v, radius, i, segments);
            aimdEmitTriangleN(ctx, style, apex, prev, next, sideNormal((float)i - 0.5f), sideNormal((float)(i - 1)), sideNormal((float)i), color, true);
            prev = next;
        }
        aimdVec3 down = axis * -1.0f;
        emitDisc(ctx, style, baseCenter, u, v, radius, color, &down);
    } else {
        emitCircleOutline(ctx, style, baseCenter, u, v, radius, color);
        for (uint32_t i = 0; i < segments; i += sideLineStep(segments))
            aimdEmitLine(ctx, style, apex, circlePoint(baseCenter, u, v, radius, i, segments), color);
    }
}

static void emitArrow(aimdContext* ctx, const aimdStyle& style, aimdVec3 from, aimdVec3 to, float headSize, uint32_t color) {
    aimdVec3 dir = to - from;
    float len = length(dir);
    if (len < 1e-6f)
        return;
    dir = dir * (1.0f / len);
    float head = std::min(headSize, len);
    aimdVec3 headBase = to - dir * head;

    // The shaft is always a line: a filled arrow only fills its head.
    if (len > head)
        aimdEmitLine(ctx, style, from, headBase, color);

    aimdStyle headStyle = style;
    headStyle.segments = std::min<uint32_t>(segmentCount(style), 16);
    emitCone(ctx, headStyle, to, headBase, head * 0.35f, color);
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
        corners[i] = transformPoint(transform, local);
    }
    emitHexahedron(ctx, style, corners);
}

void aimdAABB(aimdVec3 min, aimdVec3 max) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = { (i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z };
    emitHexahedron(ctx, style, corners);
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
        corners[i] = transformPoint(m, { x, y, z }) * (1.0f / w);
    }
    emitHexahedron(ctx, style, corners);
}

void aimdCone(aimdVec3 apex, aimdVec3 baseCenter, float radius) {
    AIMD_SHAPE_PROLOGUE();
    emitCone(ctx, style, apex, baseCenter, radius, style.color);
}

void aimdCylinder(aimdVec3 a, aimdVec3 b, float radius) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 u, v;
    basis(b - a, u, v);
    uint32_t segments = segmentCount(style);

    if (isFilled(style)) {
        aimdVec3 axis = normalize(b - a);
        aimdVec3 down = axis * -1.0f;
        emitDisc(ctx, style, a, u, v, radius, style.color, &down);
        emitDisc(ctx, style, b, u, v, radius, style.color, &axis);
        aimdVec3 prevA = circlePoint(a, u, v, radius, 0, segments);
        aimdVec3 prevB = circlePoint(b, u, v, radius, 0, segments);
        aimdVec3 prevN = circleDir(u, v, 0.0f, segments);
        for (uint32_t i = 1; i <= segments; ++i) {
            aimdVec3 nextA = circlePoint(a, u, v, radius, i, segments);
            aimdVec3 nextB = circlePoint(b, u, v, radius, i, segments);
            aimdVec3 nextN = circleDir(u, v, (float)i, segments);
            aimdEmitTriangleN(ctx, style, prevA, nextA, nextB, prevN, nextN, nextN, style.color, true);
            aimdEmitTriangleN(ctx, style, prevA, nextB, prevB, prevN, nextN, prevN, style.color, true);
            prevA = nextA;
            prevB = nextB;
            prevN = nextN;
        }
    } else {
        emitCircleOutline(ctx, style, a, u, v, radius, style.color);
        emitCircleOutline(ctx, style, b, u, v, radius, style.color);
        for (uint32_t i = 0; i < segments; i += sideLineStep(segments))
            aimdEmitLine(ctx, style, circlePoint(a, u, v, radius, i, segments), circlePoint(b, u, v, radius, i, segments), style.color);
    }
}

void aimdArrow(aimdVec3 from, aimdVec3 to, float headSize) {
    AIMD_SHAPE_PROLOGUE();
    emitArrow(ctx, style, from, to, headSize, style.color);
}

void aimdAxes(const float transform[16], float size) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 origin = { transform[12], transform[13], transform[14] };
    aimdVec3 axes[3] = {
        normalize({ transform[0], transform[1], transform[2] }),
        normalize({ transform[4], transform[5], transform[6] }),
        normalize({ transform[8], transform[9], transform[10] }),
    };
    const uint32_t colors[3] = { kColorX, kColorY, kColorZ };
    for (int i = 0; i < 3; ++i)
        emitArrow(ctx, style, origin, origin + axes[i] * size, size * 0.2f, colors[i]);
}

void aimdCircle(aimdVec3 center, aimdVec3 normal, float radius) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 u, v;
    basis(normal, u, v);
    if (isFilled(style))
        emitDisc(ctx, style, center, u, v, radius, style.color);
    else
        emitCircleOutline(ctx, style, center, u, v, radius, style.color);
}

void aimdRing(aimdVec3 center, aimdVec3 normal, float innerRadius, float outerRadius) {
    AIMD_SHAPE_PROLOGUE();
    aimdVec3 u, v;
    basis(normal, u, v);
    uint32_t segments = segmentCount(style);

    if (isFilled(style)) {
        aimdVec3 prevIn = circlePoint(center, u, v, innerRadius, 0, segments);
        aimdVec3 prevOut = circlePoint(center, u, v, outerRadius, 0, segments);
        for (uint32_t i = 1; i <= segments; ++i) {
            aimdVec3 nextIn = circlePoint(center, u, v, innerRadius, i, segments);
            aimdVec3 nextOut = circlePoint(center, u, v, outerRadius, i, segments);
            aimdEmitTriangle(ctx, style, prevIn, prevOut, nextOut, style.color);
            aimdEmitTriangle(ctx, style, prevIn, nextOut, nextIn, style.color);
            prevIn = nextIn;
            prevOut = nextOut;
        }
    } else {
        emitCircleOutline(ctx, style, center, u, v, innerRadius, style.color);
        emitCircleOutline(ctx, style, center, u, v, outerRadius, style.color);
    }
}

void aimdRings(aimdVec3 center, float radius) {
    AIMD_SHAPE_PROLOGUE();
    const aimdVec3 x = { 1.0f, 0.0f, 0.0f }, y = { 0.0f, 1.0f, 0.0f }, z = { 0.0f, 0.0f, 1.0f };
    // Each ring is colored after the axis it rotates around.
    emitCircleOutline(ctx, style, center, y, z, radius, kColorX);
    emitCircleOutline(ctx, style, center, z, x, radius, kColorY);
    emitCircleOutline(ctx, style, center, x, y, radius, kColorZ);
}

void aimdSphere(aimdVec3 center, float radius) {
    AIMD_SHAPE_PROLOGUE();
    uint32_t segments = segmentCount(style);
    aimdSphereEx(center, radius, std::max<uint32_t>(2, segments / 2), segments);
}

void aimdSphereEx(aimdVec3 center, float radius, uint32_t rings, uint32_t sectors) {
    AIMD_SHAPE_PROLOGUE();
    rings = std::clamp<uint32_t>(rings, 2, 256);
    sectors = std::clamp<uint32_t>(sectors, 3, 256);
    const aimdVec3 x = { 1.0f, 0.0f, 0.0f }, y = { 0.0f, 1.0f, 0.0f }, z = { 0.0f, 0.0f, 1.0f };

    // Unit direction at latitude index 'ring' (0 = north pole .. rings = south pole) and longitude index 'sector'.
    auto sphereDir = [&](uint32_t ring, uint32_t sector) {
        float theta = kPi * (float)ring / (float)rings;
        float phi = 2.0f * kPi * (float)sector / (float)sectors;
        return y * std::cos(theta) + (x * std::cos(phi) + z * std::sin(phi)) * std::sin(theta);
    };

    if (isFilled(style)) {
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
    basis(normal, u, v);
    float half = size * 0.5f;
    float step = size / (float)cells;
    for (uint32_t i = 0; i <= cells; ++i) {
        float offset = -half + step * (float)i;
        aimdEmitLine(ctx, style, center + u * offset - v * half, center + u * offset + v * half, style.color);
        aimdEmitLine(ctx, style, center + v * offset - u * half, center + v * offset + u * half, style.color);
    }
}
