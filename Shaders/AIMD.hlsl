/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * AIMD shaders. Entry points: LineVS, PointVS, TriangleVS (vertex), MainPS (fragment) and FinalizeCS (compute, GPU mode).
 * Self-contained on purpose (no #include) so it compiles from any working directory.
 *
 * Every primitive is drawn as triangles with vertex pulling: lines and points are expanded to screen-aligned quads
 * (6 vertices per primitive) so they can have an arbitrary pixel width.
 */

#if defined(AGFX_VULKAN)
    #define AIMD_PUSH_CONSTANTS(type, name) [[vk::push_constant]] ConstantBuffer<type> name : register(b0)
#else
    #define AIMD_PUSH_CONSTANTS(type, name) ConstantBuffer<type> name : register(b0)
#endif

// Layouts must match Source/aimd_internal.h
struct Line {
    float3 p0;
    uint color;
    float3 p1;
    float thickness;
};

struct Point {
    float3 p;
    uint color;
    float size;
    uint flags;
    float2 pad;
};

struct Vertex {
    float3 p;
    uint color;
    float3 n; // Zero: use the flat face normal
    uint flags;
};

struct PushConstants {
    column_major float4x4 viewProjection;
    float2 viewportSize;
    uint buffer;
    uint firstPrimitive;
    float depthBias;
    uint flags;
    uint2 pad;
    float3 cameraPosition;
    float pad2;
};
AIMD_PUSH_CONSTANTS(PushConstants, PC);

static const uint FLAG_REVERSE_Z = 1u << 0;
static const uint POINT_ROUND = 1u << 0;
static const uint VERTEX_SHADED = 1u << 0;
static const uint VERTEX_CLOSED = 1u << 1;

static const uint MODE_SOLID = 0;
static const uint MODE_LINE = 1;
static const uint MODE_ROUND = 2;
static const uint MODE_SHADED = 3;

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    // Pixel offset from the primitive's center line / center point
    float2 local : TEXCOORD0;
    // x: half width (or radius) in pixels, y: MODE_*
    nointerpolation float2 shape : TEXCOORD1;
    // Triangles only
    float3 worldPosition : TEXCOORD2;
    float3 normal : TEXCOORD3;
    nointerpolation uint triangleFlags : TEXCOORD4; // VERTEX_*
};

float4 UnpackColor(uint c) {
    return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF) / 255.0;
}

bool ReverseZ() {
    return (PC.flags & FLAG_REVERSE_Z) != 0;
}

// Signed distance to the near clip plane in clip space; negative means clipped.
float NearDistance(float4 c) {
    return ReverseZ() ? (c.w - c.z) : c.z;
}

// Pulls the depth toward the camera, proportionally to view distance.
float4 ApplyDepthBias(float4 c) {
    if (ReverseZ())
        c.z = min(c.z * (1.0 + PC.depthBias), c.w);
    else
        c.z = max(c.w - (c.w - c.z) * (1.0 + PC.depthBias), 0.0);
    return c;
}

float2 ToPixels(float4 c) {
    return c.xy / c.w * 0.5 * PC.viewportSize;
}

// Moves a clip-space position by an offset in pixels.
float4 OffsetPixels(float4 c, float2 offset) {
    c.xy += offset / (0.5 * PC.viewportSize) * c.w;
    return c;
}

// Corner of a quad drawn as two triangles, (0,0) (1,0) (1,1) / (0,0) (1,1) (0,1), for vertex i in [0, 6).
// Arithmetic rather than a constant table: dynamically indexed global constants are an invalid load under Metal Shader
// Converter's shader validation.
uint2 QuadCorner(uint i) {
    uint q = i < 3 ? i : (i == 3 ? 0 : i - 2);
    return uint2(q == 1 || q == 2, q >= 2);
}

VertexOut Degenerate() {
    VertexOut o = (VertexOut)0;
    o.position = float4(0.0, 0.0, 0.0, 1.0);
    return o;
}

VertexOut LineVS(uint vertexID : SV_VertexID) {
    StructuredBuffer<Line> lines = ResourceDescriptorHeap[PC.buffer];
    Line l = lines[PC.firstPrimitive + vertexID / 6];

    float4 c0 = mul(PC.viewProjection, float4(l.p0, 1.0));
    float4 c1 = mul(PC.viewProjection, float4(l.p1, 1.0));

    // Clip the segment against the near plane so the screen-space expansion stays valid.
    float d0 = NearDistance(c0);
    float d1 = NearDistance(c1);
    if (d0 < 0.0 && d1 < 0.0)
        return Degenerate();
    if (d0 < 0.0)
        c0 = lerp(c0, c1, d0 / (d0 - d1));
    else if (d1 < 0.0)
        c1 = lerp(c1, c0, d1 / (d1 - d0));

    float2 s0 = ToPixels(c0);
    float2 s1 = ToPixels(c1);
    float2 delta = s1 - s0;
    float2 dir = dot(delta, delta) > 1e-8 ? normalize(delta) : float2(1.0, 0.0);
    float2 normal = float2(-dir.y, dir.x);

    // x: start/end of the segment, y: side
    uint2 corner = QuadCorner(vertexID % 6);
    float side = corner.y ? 1.0 : -1.0;

    float halfWidth = max(l.thickness, 0.0) * 0.5;
    float extent = halfWidth + 1.0; // 1px fringe for anti-aliasing
    float4 c = corner.x ? c1 : c0;
    // Square caps (extend by half the width) so polylines join without gaps.
    float2 offset = normal * (side * extent) + dir * ((corner.x ? 1.0 : -1.0) * halfWidth);

    VertexOut o = (VertexOut)0;
    o.position = ApplyDepthBias(OffsetPixels(c, offset));
    o.color = UnpackColor(l.color);
    o.local = float2(0.0, side * extent);
    o.shape = float2(halfWidth, MODE_LINE);
    return o;
}

VertexOut PointVS(uint vertexID : SV_VertexID) {
    StructuredBuffer<Point> points = ResourceDescriptorHeap[PC.buffer];
    Point p = points[PC.firstPrimitive + vertexID / 6];

    float4 c = mul(PC.viewProjection, float4(p.p, 1.0));
    if (NearDistance(c) < 0.0)
        return Degenerate();

    float2 corner = float2(QuadCorner(vertexID % 6)) * 2.0 - 1.0;

    bool isRound = (p.flags & POINT_ROUND) != 0;
    float radius = max(p.size, 0.0) * 0.5;
    float extent = isRound ? radius + 1.0 : radius;

    VertexOut o = (VertexOut)0;
    o.position = ApplyDepthBias(OffsetPixels(c, corner * extent));
    o.color = UnpackColor(p.color);
    o.local = corner * extent;
    o.shape = float2(radius, isRound ? MODE_ROUND : MODE_SOLID);
    return o;
}

VertexOut TriangleVS(uint vertexID : SV_VertexID) {
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[PC.buffer];
    Vertex v = vertices[PC.firstPrimitive + vertexID];

    VertexOut o = (VertexOut)0;
    o.position = mul(PC.viewProjection, float4(v.p, 1.0));
    o.color = UnpackColor(v.color);
    o.shape = float2(0.0, (v.flags & VERTEX_SHADED) ? MODE_SHADED : MODE_SOLID);
    o.worldPosition = v.p;
    o.normal = v.n;
    o.triangleFlags = v.flags;
    return o;
}

// Deliberately simple lighting: one fixed key light, a sky/ground hemisphere ambient and a faint camera-facing term.
float3 Shade(float3 color, float3 worldPosition, float3 normal, float3 faceNormal) {
    // Flat shapes carry no normal and use the face normal instead.
    float3 n = normalize(dot(normal, normal) > 1e-8 ? normal : faceNormal);
    float3 toCamera = normalize(PC.cameraPosition - worldPosition);
    // Two-sided: always light the side facing the viewer.
    if (dot(n, toCamera) < 0.0)
        n = -n;

    const float3 lightDir = normalize(float3(0.35, 0.85, 0.4));
    float diffuse = saturate(dot(n, lightDir));
    float ambient = lerp(0.3, 0.55, n.y * 0.5 + 0.5);
    float facing = saturate(dot(n, toCamera));
    return color * (ambient + diffuse * 0.6 + facing * 0.15);
}

float4 MainPS(VertexOut input) : SV_Target0 {
    // Face normal from screen-space derivatives. Computed before any branching so the derivatives are well defined.
    float3 faceNormal = cross(ddy(input.worldPosition), ddx(input.worldPosition));

    // Back-face culling for closed shapes that doesn't depend on winding order: the face normal's sign is arbitrary,
    // so orient it with the (outward) vertex normal, then drop faces pointing away from the camera.
    if (input.triangleFlags & VERTEX_CLOSED) {
        float3 outward = dot(faceNormal, input.normal) < 0.0 ? -faceNormal : faceNormal;
        if (dot(outward, PC.cameraPosition - input.worldPosition) < 0.0)
            discard;
    }

    float4 color = input.color;
    uint mode = (uint)input.shape.y;
    float halfWidth = input.shape.x;

    // Coverage of a pixel-wide filter against the shape edge; also fades lines thinner than a pixel.
    if (mode == MODE_LINE)
        color.a *= saturate(halfWidth + 0.5 - abs(input.local.y));
    else if (mode == MODE_ROUND)
        color.a *= saturate(halfWidth + 0.5 - length(input.local));
    else if (mode == MODE_SHADED)
        color.rgb = Shade(color.rgb, input.worldPosition, input.normal, faceNormal);

    if (color.a <= 0.0)
        discard;
    return color;
}

//
// GPU-driven path: turns the per-region counters written by AIMDDebugRenderer (Shaders/AIMDDebug.hlsli) into indirect
// draw commands. One thread per region; region = kind * 2 + bucket with kinds triangles, lines, points.
//

struct FinalizeConstants {
    uint header;   // aimdGpuHeader: [0] geometry, [1] counters, [2..7] region offsets, [8..13] region capacities
    uint commands; // Indirect bundle commands (agfxDrawCommand, 20 bytes each)
    uint counts;   // Indirect bundle counts
    uint pad;
};
AIMD_PUSH_CONSTANTS(FinalizeConstants, FinalizePC);

static const uint REGION_COUNT = 6;

[numthreads(8, 1, 1)]
void FinalizeCS(uint region : SV_DispatchThreadID) {
    if (region >= REGION_COUNT)
        return;

    ByteAddressBuffer header = ResourceDescriptorHeap[FinalizePC.header];
    RWByteAddressBuffer counters = ResourceDescriptorHeap[header.Load(4)];
    uint capacity = header.Load((8 + region) * 4);

    // Counters hold the requested amount, which may exceed the capacity: shapes that didn't fit were dropped.
    uint primitives = min(counters.Load(region * 4), capacity);
    bool triangles = region < 2;
    uint vertexCount = triangles ? primitives : primitives * 6;

    RWByteAddressBuffer commands = ResourceDescriptorHeap[FinalizePC.commands];
    RWByteAddressBuffer counts = ResourceDescriptorHeap[FinalizePC.counts];
    uint offset = region * 20;
    commands.Store(offset + 0, region);      // drawID
    commands.Store(offset + 4, vertexCount);
    commands.Store(offset + 8, 1);           // instanceCount
    commands.Store(offset + 12, 0);          // firstVertex (the region offset comes through push constants)
    commands.Store(offset + 16, 0);          // firstInstance
    counts.Store(region * 4, vertexCount > 0 ? 1 : 0);
}
