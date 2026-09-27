/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * AIMD GPU-driven debug drawing. Include from any shader (typically compute) recorded between aimdGpuBeginFrame() and
 * aimdExecute(), then:
 *
 *     AIMDDebugRenderer renderer = AIMDDebugRenderer::Create(aimdHandle); // Handle returned by aimdGpuBeginFrame
 *     renderer.SetColor(float4(0.0, 1.0, 0.0, 1.0));
 *     renderer.SetFilled(true);
 *     renderer.DrawBox(instanceTransform);
 *
 * The renderer is stateful like the C API: setters change the style used by the following Draw* calls.
 * Matrices are applied as mul(m, float4(p, 1.0)) (column vectors, glm-style upload).
 * Shapes are appended with one atomic per shape; a shape that doesn't fit in AIMD's buffers is dropped (enable
 * aimdContextCreateInfo::gpuAutoGrow to let AIMD grow them).
 *
 * Self-contained: only needs SM 6.6 ResourceDescriptorHeap. Every name is prefixed with AIMD.
 * Layouts must match Source/aimd_internal.h (aimdGpuHeader, aimdGpuLine, aimdGpuPoint, aimdGpuVertex).
 */

#ifndef AIMD_DEBUG_HLSLI
#define AIMD_DEBUG_HLSLI

// Style flags (aimdStyleFlags)
static const uint AIMD_STYLE_FILLED = 1u << 0;
static const uint AIMD_STYLE_NO_DEPTH_TEST = 1u << 1;
static const uint AIMD_STYLE_ROUND_POINTS = 1u << 2;
static const uint AIMD_STYLE_SHADED = 1u << 4;

// Internal
static const uint AIMD_KIND_TRIANGLES = 0;
static const uint AIMD_KIND_LINES = 1;
static const uint AIMD_KIND_POINTS = 2;
static const uint AIMD_INVALID_SLOT = 0xFFFFFFFFu;
static const uint AIMD_SLOT_SIZE = 32;
static const uint AIMD_POINT_ROUND = 1u << 0;
static const uint AIMD_VERTEX_SHADED = 1u << 0;
static const uint AIMD_VERTEX_CLOSED = 1u << 1;
static const float AIMD_PI = 3.14159265358979323846;
static const uint AIMD_COLOR_X = 0xFF3440EBu; // AIMD_RGBA(235, 64, 52, 255)
static const uint AIMD_COLOR_Y = 0xFF46C85Au; // AIMD_RGBA(90, 200, 70, 255)
static const uint AIMD_COLOR_Z = 0xFFF0783Cu; // AIMD_RGBA(60, 120, 240, 255)

// Corner i of a hexahedron has x from bit 0, y from bit 1, z from bit 2.
// Only index these from [unroll]ed loops: dynamically indexed global constants are an invalid load under Metal Shader
// Converter's shader validation.
static const uint2 AIMD_BOX_EDGES[12] = {
    uint2(0, 1), uint2(2, 3), uint2(4, 5), uint2(6, 7),
    uint2(0, 2), uint2(1, 3), uint2(4, 6), uint2(5, 7),
    uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7),
};
static const uint4 AIMD_BOX_FACES[6] = {
    uint4(0, 2, 6, 4), uint4(1, 5, 7, 3),
    uint4(0, 4, 5, 1), uint4(2, 3, 7, 6),
    uint4(0, 1, 3, 2), uint4(4, 6, 7, 5),
};

struct AIMDStyle {
    uint color;      // Packed RGBA8, R in the lowest byte
    float thickness; // Line thickness in pixels
    float pointSize; // Point diameter in pixels
    uint segments;   // Tessellation of round shapes, clamped to [3, 256]
    uint flags;      // AIMD_STYLE_*
};

uint AIMDRgba(uint r, uint g, uint b, uint a) {
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | ((a & 0xFF) << 24);
}

uint AIMDColor(float4 color) {
    uint4 c = (uint4)round(saturate(color) * 255.0);
    return AIMDRgba(c.r, c.g, c.b, c.a);
}

AIMDStyle AIMDDefaultStyle() {
    AIMDStyle style;
    style.color = 0xFFFFFFFFu;
    style.thickness = 2.0;
    style.pointSize = 6.0;
    style.segments = 24;
    style.flags = 0;
    return style;
}

float3 AIMDNormalize(float3 v) {
    float len = length(v);
    return len > 1e-12 ? v / len : float3(0.0, 1.0, 0.0);
}

// Two unit vectors perpendicular to n and to each other.
void AIMDBasis(float3 n, out float3 u, out float3 v) {
    n = AIMDNormalize(n);
    float3 helper = abs(n.x) > 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    u = AIMDNormalize(cross(helper, n));
    v = cross(n, u);
}

float3 AIMDCircleDir(float3 u, float3 v, float i, uint segments) {
    float angle = 2.0 * AIMD_PI * i / (float)segments;
    return u * cos(angle) + v * sin(angle);
}

float3 AIMDCirclePoint(float3 center, float3 u, float3 v, float radius, uint i, uint segments) {
    return center + AIMDCircleDir(u, v, (float)i, segments) * radius;
}

struct AIMDDebugRenderer {
    uint header;
    uint geometry;
    uint counters;
    AIMDStyle style;

    static AIMDDebugRenderer Create(uint handle) {
        ByteAddressBuffer headerBuffer = ResourceDescriptorHeap[handle];
        AIMDDebugRenderer renderer;
        renderer.header = handle;
        renderer.geometry = headerBuffer.Load(0);
        renderer.counters = headerBuffer.Load(4);
        renderer.style = AIMDDefaultStyle();
        return renderer;
    }

    //
    // Style
    //

    void SetStyle(AIMDStyle newStyle) { style = newStyle; }
    void SetColor(uint color) { style.color = color; }
    void SetColor(float4 color) { style.color = AIMDColor(color); }
    void SetThickness(float thickness) { style.thickness = thickness; }
    void SetPointSize(float pointSize) { style.pointSize = pointSize; }
    void SetSegments(uint segments) { style.segments = segments; }
    void SetFlag(uint flag, bool enabled) { style.flags = enabled ? (style.flags | flag) : (style.flags & ~flag); }
    void SetFilled(bool filled) { SetFlag(AIMD_STYLE_FILLED, filled); }
    void SetShaded(bool shaded) { SetFlag(AIMD_STYLE_SHADED, shaded); }
    void SetDepthTest(bool depthTest) { SetFlag(AIMD_STYLE_NO_DEPTH_TEST, !depthTest); }
    void SetRoundPoints(bool round) { SetFlag(AIMD_STYLE_ROUND_POINTS, round); }

    //
    // Internals
    //

    bool IsFilled() { return (style.flags & AIMD_STYLE_FILLED) != 0; }
    uint Segments() { return clamp(style.segments, 3u, 256u); }

    // Reserves n consecutive slots of a kind, in the region of the current depth bucket.
    // Returns the absolute slot of the first one, or AIMD_INVALID_SLOT if they don't fit (the shape is dropped).
    uint Reserve(uint kind, uint n) {
        if (n == 0)
            return AIMD_INVALID_SLOT;
        uint region = kind * 2 + ((style.flags & AIMD_STYLE_NO_DEPTH_TEST) ? 1 : 0);
        ByteAddressBuffer headerBuffer = ResourceDescriptorHeap[header];
        uint offset = headerBuffer.Load(8 + region * 4);
        uint capacity = headerBuffer.Load(32 + region * 4);

        RWByteAddressBuffer counterBuffer = ResourceDescriptorHeap[counters];
        uint base;
        counterBuffer.InterlockedAdd(region * 4, n, base);
        if (base + n <= capacity)
            return offset + base;

        // Didn't fit. The counter still grew (so auto-grow sees the demand), and the slots below the capacity that this
        // reservation covers will be drawn: fill them with invisible primitives (alpha 0, degenerate).
        RWByteAddressBuffer geometryBuffer = ResourceDescriptorHeap[geometry];
        uint end = min(base + n, capacity);
        for (uint slot = base; slot < end; ++slot) {
            geometryBuffer.Store4((offset + slot) * AIMD_SLOT_SIZE, uint4(0, 0, 0, 0));
            geometryBuffer.Store4((offset + slot) * AIMD_SLOT_SIZE + 16, uint4(0, 0, 0, 0));
        }
        return AIMD_INVALID_SLOT;
    }

    void StoreLine(uint slot, float3 a, float3 b, uint color) {
        RWByteAddressBuffer geometryBuffer = ResourceDescriptorHeap[geometry];
        geometryBuffer.Store4(slot * AIMD_SLOT_SIZE, uint4(asuint(a), color));
        geometryBuffer.Store4(slot * AIMD_SLOT_SIZE + 16, uint4(asuint(b), asuint(style.thickness)));
    }

    void StorePoint(uint slot, float3 p, uint color) {
        RWByteAddressBuffer geometryBuffer = ResourceDescriptorHeap[geometry];
        uint flags = (style.flags & AIMD_STYLE_ROUND_POINTS) ? AIMD_POINT_ROUND : 0;
        geometryBuffer.Store4(slot * AIMD_SLOT_SIZE, uint4(asuint(p), color));
        geometryBuffer.Store4(slot * AIMD_SLOT_SIZE + 16, uint4(asuint(style.pointSize), flags, 0, 0));
    }

    // Normals must point out of the surface when 'closed' is set: the pixel shader culls back faces with them.
    // A zero normal makes the pixel shader use the flat face normal.
    void StoreTriangle(uint slot, float3 a, float3 b, float3 c, float3 na, float3 nb, float3 nc, uint color, bool closed) {
        RWByteAddressBuffer geometryBuffer = ResourceDescriptorHeap[geometry];
        uint flags = ((style.flags & AIMD_STYLE_SHADED) ? AIMD_VERTEX_SHADED : 0) | (closed ? AIMD_VERTEX_CLOSED : 0);
        uint address = slot * AIMD_SLOT_SIZE;
        geometryBuffer.Store4(address + 0, uint4(asuint(a), color));
        geometryBuffer.Store4(address + 16, uint4(asuint(na), flags));
        geometryBuffer.Store4(address + 32, uint4(asuint(b), color));
        geometryBuffer.Store4(address + 48, uint4(asuint(nb), flags));
        geometryBuffer.Store4(address + 64, uint4(asuint(c), color));
        geometryBuffer.Store4(address + 80, uint4(asuint(nc), flags));
    }

    void EmitHexahedron(float3 corners[8]) {
        if (IsFilled()) {
            uint slot = Reserve(AIMD_KIND_TRIANGLES, 36);
            if (slot == AIMD_INVALID_SLOT)
                return;
            float3 center = 0.0;
            [unroll]
            for (uint i = 0; i < 8; ++i)
                center += corners[i] * 0.125;
            // Unrolled so the table lookups are compile-time constants and corners[] stays in registers.
            [unroll]
            for (uint f = 0; f < 6; ++f) {
                uint4 face = AIMD_BOX_FACES[f];
                float3 a = corners[face.x], b = corners[face.y], c = corners[face.z], d = corners[face.w];
                // Oriented away from the center, so any transform (even a mirroring one) works.
                float3 n = AIMDNormalize(cross(c - a, b - a) + cross(d - a, c - a));
                if (dot(n, (a + c) * 0.5 - center) < 0.0)
                    n = -n;
                StoreTriangle(slot + f * 6, a, b, c, n, n, n, style.color, true);
                StoreTriangle(slot + f * 6 + 3, a, c, d, n, n, n, style.color, true);
            }
        } else {
            uint slot = Reserve(AIMD_KIND_LINES, 12);
            if (slot == AIMD_INVALID_SLOT)
                return;
            [unroll]
            for (uint e = 0; e < 12; ++e)
                StoreLine(slot + e, corners[AIMD_BOX_EDGES[e].x], corners[AIMD_BOX_EDGES[e].y], style.color);
        }
    }

    void EmitCircleOutline(float3 center, float3 u, float3 v, float radius, uint color) {
        uint segments = Segments();
        uint slot = Reserve(AIMD_KIND_LINES, segments);
        if (slot == AIMD_INVALID_SLOT)
            return;
        [loop]
        for (uint i = 0; i < segments; ++i)
            StoreLine(slot + i, AIMDCirclePoint(center, u, v, radius, i, segments), AIMDCirclePoint(center, u, v, radius, i + 1, segments), color);
    }

    void EmitCone(float3 apex, float3 baseCenter, float radius, uint color, uint segments) {
        float3 u, v;
        AIMDBasis(apex - baseCenter, u, v);

        if (IsFilled()) {
            uint slot = Reserve(AIMD_KIND_TRIANGLES, segments * 6);
            if (slot == AIMD_INVALID_SLOT)
                return;
            float3 axis = apex - baseCenter;
            float height = length(axis);
            axis = AIMDNormalize(axis);
            [loop]
            for (uint i = 1; i <= segments; ++i) {
                float3 prev = AIMDCirclePoint(baseCenter, u, v, radius, i - 1, segments);
                float3 next = AIMDCirclePoint(baseCenter, u, v, radius, i, segments);
                // Side normal: the radial direction tilted toward the apex by the cone's slope.
                float3 nMid = AIMDNormalize(AIMDCircleDir(u, v, (float)i - 0.5, segments) * height + axis * radius);
                float3 nPrev = AIMDNormalize(AIMDCircleDir(u, v, (float)(i - 1), segments) * height + axis * radius);
                float3 nNext = AIMDNormalize(AIMDCircleDir(u, v, (float)i, segments) * height + axis * radius);
                uint base = slot + (i - 1) * 6;
                StoreTriangle(base, apex, prev, next, nMid, nPrev, nNext, color, true);
                StoreTriangle(base + 3, baseCenter, next, prev, -axis, -axis, -axis, color, true);
            }
        } else {
            uint step = max(1u, segments / 8);
            uint sideLines = (segments + step - 1) / step;
            uint slot = Reserve(AIMD_KIND_LINES, segments + sideLines);
            if (slot == AIMD_INVALID_SLOT)
                return;
            [loop]
            for (uint i = 0; i < segments; ++i)
                StoreLine(slot + i, AIMDCirclePoint(baseCenter, u, v, radius, i, segments), AIMDCirclePoint(baseCenter, u, v, radius, i + 1, segments), color);
            [loop]
            for (uint k = 0; k < sideLines; ++k)
                StoreLine(slot + segments + k, apex, AIMDCirclePoint(baseCenter, u, v, radius, k * step, segments), color);
        }
    }

    void EmitArrow(float3 from, float3 to, float headSize, uint color) {
        float3 dir = to - from;
        float len = length(dir);
        if (len < 1e-6)
            return;
        dir /= len;
        float head = min(headSize, len);
        float3 headBase = to - dir * head;

        // The shaft is always a line: a filled arrow only fills its head.
        if (len > head) {
            uint slot = Reserve(AIMD_KIND_LINES, 1);
            if (slot != AIMD_INVALID_SLOT)
                StoreLine(slot, from, headBase, color);
        }
        EmitCone(to, headBase, head * 0.35, color, min(Segments(), 16u));
    }

    //
    // Primitives
    //

    void DrawLine(float3 a, float3 b) {
        uint slot = Reserve(AIMD_KIND_LINES, 1);
        if (slot != AIMD_INVALID_SLOT)
            StoreLine(slot, a, b, style.color);
    }

    void DrawPoint(float3 p) {
        uint slot = Reserve(AIMD_KIND_POINTS, 1);
        if (slot != AIMD_INVALID_SLOT)
            StorePoint(slot, p, style.color);
    }

    // Filled with AIMD_STYLE_FILLED, outlined otherwise. Two-sided.
    void DrawTriangle(float3 a, float3 b, float3 c) {
        if (IsFilled()) {
            uint slot = Reserve(AIMD_KIND_TRIANGLES, 3);
            if (slot != AIMD_INVALID_SLOT)
                StoreTriangle(slot, a, b, c, 0.0, 0.0, 0.0, style.color, false);
        } else {
            uint slot = Reserve(AIMD_KIND_LINES, 3);
            if (slot == AIMD_INVALID_SLOT)
                return;
            StoreLine(slot + 0, a, b, style.color);
            StoreLine(slot + 1, b, c, style.color);
            StoreLine(slot + 2, c, a, style.color);
        }
    }

    // Corners in winding order. Two-sided.
    void DrawQuad(float3 a, float3 b, float3 c, float3 d) {
        if (IsFilled()) {
            uint slot = Reserve(AIMD_KIND_TRIANGLES, 6);
            if (slot == AIMD_INVALID_SLOT)
                return;
            StoreTriangle(slot, a, b, c, 0.0, 0.0, 0.0, style.color, false);
            StoreTriangle(slot + 3, a, c, d, 0.0, 0.0, 0.0, style.color, false);
        } else {
            uint slot = Reserve(AIMD_KIND_LINES, 4);
            if (slot == AIMD_INVALID_SLOT)
                return;
            StoreLine(slot + 0, a, b, style.color);
            StoreLine(slot + 1, b, c, style.color);
            StoreLine(slot + 2, c, d, style.color);
            StoreLine(slot + 3, d, a, style.color);
        }
    }

    //
    // Shapes. Closed ones (box, AABB, frustum, cone, cylinder, sphere, arrow heads) only draw their camera-facing
    // side when filled; open ones (triangle, quad, circle, ring) are two-sided.
    //

    // Unit cube ([-0.5, 0.5]^3) transformed by 'transform'.
    void DrawBox(float4x4 transform) {
        float3 corners[8];
        [unroll]
        for (uint i = 0; i < 8; ++i) {
            float3 local = float3((i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5, (i & 4) ? 0.5 : -0.5);
            corners[i] = mul(transform, float4(local, 1.0)).xyz;
        }
        EmitHexahedron(corners);
    }

    void DrawAABB(float3 minCorner, float3 maxCorner) {
        float3 corners[8];
        [unroll]
        for (uint i = 0; i < 8; ++i)
            corners[i] = float3((i & 1) ? maxCorner.x : minCorner.x, (i & 2) ? maxCorner.y : minCorner.y, (i & 4) ? maxCorner.z : minCorner.z);
        EmitHexahedron(corners);
    }

    // The NDC cube ([-1,1]x[-1,1]x[0,1]) transformed by invViewProjection. Infinite projections are skipped.
    void DrawFrustum(float4x4 invViewProjection) {
        float3 corners[8];
        [unroll]
        for (uint i = 0; i < 8; ++i) {
            float4 ndc = float4((i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : 0.0, 1.0);
            float4 world = mul(invViewProjection, ndc);
            if (abs(world.w) < 1e-7)
                return;
            corners[i] = world.xyz / world.w;
        }
        EmitHexahedron(corners);
    }

    void DrawCone(float3 apex, float3 baseCenter, float radius) {
        EmitCone(apex, baseCenter, radius, style.color, Segments());
    }

    void DrawCylinder(float3 a, float3 b, float radius) {
        float3 u, v;
        AIMDBasis(b - a, u, v);
        uint segments = Segments();

        if (IsFilled()) {
            uint slot = Reserve(AIMD_KIND_TRIANGLES, segments * 12);
            if (slot == AIMD_INVALID_SLOT)
                return;
            float3 axis = AIMDNormalize(b - a);
            [loop]
            for (uint i = 1; i <= segments; ++i) {
                float3 prevA = AIMDCirclePoint(a, u, v, radius, i - 1, segments);
                float3 nextA = AIMDCirclePoint(a, u, v, radius, i, segments);
                float3 prevB = AIMDCirclePoint(b, u, v, radius, i - 1, segments);
                float3 nextB = AIMDCirclePoint(b, u, v, radius, i, segments);
                float3 prevN = AIMDCircleDir(u, v, (float)(i - 1), segments);
                float3 nextN = AIMDCircleDir(u, v, (float)i, segments);
                uint base = slot + (i - 1) * 12;
                StoreTriangle(base + 0, a, prevA, nextA, -axis, -axis, -axis, style.color, true);
                StoreTriangle(base + 3, b, prevB, nextB, axis, axis, axis, style.color, true);
                StoreTriangle(base + 6, prevA, nextA, nextB, prevN, nextN, nextN, style.color, true);
                StoreTriangle(base + 9, prevA, nextB, prevB, prevN, nextN, prevN, style.color, true);
            }
        } else {
            uint step = max(1u, segments / 8);
            uint sideLines = (segments + step - 1) / step;
            uint slot = Reserve(AIMD_KIND_LINES, segments * 2 + sideLines);
            if (slot == AIMD_INVALID_SLOT)
                return;
            [loop]
            for (uint i = 0; i < segments; ++i) {
                StoreLine(slot + i * 2, AIMDCirclePoint(a, u, v, radius, i, segments), AIMDCirclePoint(a, u, v, radius, i + 1, segments), style.color);
                StoreLine(slot + i * 2 + 1, AIMDCirclePoint(b, u, v, radius, i, segments), AIMDCirclePoint(b, u, v, radius, i + 1, segments), style.color);
            }
            [loop]
            for (uint k = 0; k < sideLines; ++k)
                StoreLine(slot + segments * 2 + k, AIMDCirclePoint(a, u, v, radius, k * step, segments), AIMDCirclePoint(b, u, v, radius, k * step, segments), style.color);
        }
    }

    // Line with a cone head of length headSize. Only the head is filled with AIMD_STYLE_FILLED.
    void DrawArrow(float3 from, float3 to, float headSize) {
        EmitArrow(from, to, headSize, style.color);
    }

    // X/Y/Z arrows (red/green/blue) along the transform's basis. Ignores the style color.
    void DrawAxes(float4x4 transform, float size) {
        float3 origin = mul(transform, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 x = AIMDNormalize(mul(transform, float4(1.0, 0.0, 0.0, 0.0)).xyz);
        float3 y = AIMDNormalize(mul(transform, float4(0.0, 1.0, 0.0, 0.0)).xyz);
        float3 z = AIMDNormalize(mul(transform, float4(0.0, 0.0, 1.0, 0.0)).xyz);
        EmitArrow(origin, origin + x * size, size * 0.2, AIMD_COLOR_X);
        EmitArrow(origin, origin + y * size, size * 0.2, AIMD_COLOR_Y);
        EmitArrow(origin, origin + z * size, size * 0.2, AIMD_COLOR_Z);
    }

    void DrawCircle(float3 center, float3 normal, float radius) {
        float3 u, v;
        AIMDBasis(normal, u, v);
        if (!IsFilled()) {
            EmitCircleOutline(center, u, v, radius, style.color);
            return;
        }
        uint segments = Segments();
        uint slot = Reserve(AIMD_KIND_TRIANGLES, segments * 3);
        if (slot == AIMD_INVALID_SLOT)
            return;
        [loop]
        for (uint i = 0; i < segments; ++i)
            StoreTriangle(slot + i * 3, center, AIMDCirclePoint(center, u, v, radius, i, segments), AIMDCirclePoint(center, u, v, radius, i + 1, segments),
                          0.0, 0.0, 0.0, style.color, false);
    }

    // Annulus between two radii. Outlined as two circles when not filled.
    void DrawRing(float3 center, float3 normal, float innerRadius, float outerRadius) {
        float3 u, v;
        AIMDBasis(normal, u, v);
        if (!IsFilled()) {
            EmitCircleOutline(center, u, v, innerRadius, style.color);
            EmitCircleOutline(center, u, v, outerRadius, style.color);
            return;
        }
        uint segments = Segments();
        uint slot = Reserve(AIMD_KIND_TRIANGLES, segments * 6);
        if (slot == AIMD_INVALID_SLOT)
            return;
        [loop]
        for (uint i = 0; i < segments; ++i) {
            float3 prevIn = AIMDCirclePoint(center, u, v, innerRadius, i, segments);
            float3 prevOut = AIMDCirclePoint(center, u, v, outerRadius, i, segments);
            float3 nextIn = AIMDCirclePoint(center, u, v, innerRadius, i + 1, segments);
            float3 nextOut = AIMDCirclePoint(center, u, v, outerRadius, i + 1, segments);
            StoreTriangle(slot + i * 6, prevIn, prevOut, nextOut, 0.0, 0.0, 0.0, style.color, false);
            StoreTriangle(slot + i * 6 + 3, prevIn, nextOut, nextIn, 0.0, 0.0, 0.0, style.color, false);
        }
    }

    // Three axis-aligned circles, colored after the axis they rotate around. Always outlined.
    void DrawRings(float3 center, float radius) {
        const float3 x = float3(1.0, 0.0, 0.0), y = float3(0.0, 1.0, 0.0), z = float3(0.0, 0.0, 1.0);
        EmitCircleOutline(center, y, z, radius, AIMD_COLOR_X);
        EmitCircleOutline(center, z, x, radius, AIMD_COLOR_Y);
        EmitCircleOutline(center, x, y, radius, AIMD_COLOR_Z);
    }

    // Unit direction at latitude index 'ring' (0 = north pole .. rings = south pole) and longitude index 'sector'.
    float3 SphereDir(uint ring, uint sector, uint rings, uint sectors) {
        float theta = AIMD_PI * (float)ring / (float)rings;
        float phi = 2.0 * AIMD_PI * (float)sector / (float)sectors;
        return float3(cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta));
    }

    // UV sphere with 'rings' latitude bands and 'sectors' longitude slices. The wireframe is the tessellation's edges.
    void DrawSphereEx(float3 center, float radius, uint rings, uint sectors) {
        rings = clamp(rings, 2u, 256u);
        sectors = clamp(sectors, 3u, 256u);

        if (IsFilled()) {
            // The pole rows collapse to a single triangle per sector.
            uint slot = Reserve(AIMD_KIND_TRIANGLES, (rings * 2 - 2) * sectors * 3);
            if (slot == AIMD_INVALID_SLOT)
                return;
            [loop]
            for (uint r = 0; r < rings; ++r) {
                [loop]
                for (uint s = 0; s < sectors; ++s) {
                    float3 na = SphereDir(r, s, rings, sectors), nb = SphereDir(r, s + 1, rings, sectors);
                    float3 nc = SphereDir(r + 1, s + 1, rings, sectors), nd = SphereDir(r + 1, s, rings, sectors);
                    float3 a = center + na * radius, b = center + nb * radius;
                    float3 c = center + nc * radius, d = center + nd * radius;
                    if (r != 0) {
                        StoreTriangle(slot, a, b, c, na, nb, nc, style.color, true);
                        slot += 3;
                    }
                    if (r != rings - 1) {
                        StoreTriangle(slot, a, c, d, na, nc, nd, style.color, true);
                        slot += 3;
                    }
                }
            }
        } else {
            uint slot = Reserve(AIMD_KIND_LINES, (rings - 1) * sectors + sectors * rings);
            if (slot == AIMD_INVALID_SLOT)
                return;
            // Latitude circles, then pole-to-pole meridians.
            [loop]
            for (uint r = 1; r < rings; ++r) {
                [loop]
                for (uint s = 0; s < sectors; ++s)
                    StoreLine(slot++, center + SphereDir(r, s, rings, sectors) * radius, center + SphereDir(r, s + 1, rings, sectors) * radius, style.color);
            }
            [loop]
            for (uint s2 = 0; s2 < sectors; ++s2) {
                [loop]
                for (uint r2 = 0; r2 < rings; ++r2)
                    StoreLine(slot++, center + SphereDir(r2, s2, rings, sectors) * radius, center + SphereDir(r2 + 1, s2, rings, sectors) * radius, style.color);
            }
        }
    }

    // UV sphere with Segments() / 2 rings and Segments() sectors.
    void DrawSphere(float3 center, float radius) {
        uint segments = Segments();
        DrawSphereEx(center, radius, max(2u, segments / 2), segments);
    }

    // Square grid of cells x cells, of total width 'size', in the plane with the given normal. Always lines.
    void DrawGrid(float3 center, float3 normal, float size, uint cells) {
        if (cells == 0)
            return;
        float3 u, v;
        AIMDBasis(normal, u, v);
        uint slot = Reserve(AIMD_KIND_LINES, (cells + 1) * 2);
        if (slot == AIMD_INVALID_SLOT)
            return;
        float halfSize = size * 0.5;
        float step = size / (float)cells;
        [loop]
        for (uint i = 0; i <= cells; ++i) {
            float offset = -halfSize + step * (float)i;
            StoreLine(slot + i * 2, center + u * offset - v * halfSize, center + u * offset + v * halfSize, style.color);
            StoreLine(slot + i * 2 + 1, center + v * offset - u * halfSize, center + v * offset + u * halfSize, style.color);
        }
    }

};

#endif // AIMD_DEBUG_HLSLI
