/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * AIMD -- Amélie's Immediate Mode Debug renderer.
 *
 * Usage:
 *   1. Compile Shaders/AIMD.hlsl (entry points LineVS, PointVS, TriangleVS, MainPS) with agfx_shader and
 *      create the shader modules. AIMD takes ownership of them.
 *   2. aimdContextCreate() once. The new context becomes the current one.
 *   3. Anywhere during the frame, call the aimd* shape functions. They use the current style (see aimdPushStyle).
 *   4. aimdExecute() once per frame: uploads, renders into the given targets, and hands projected text
 *      labels to your callback. Batches are then cleared for the next frame.
 *
 * GPU-driven usage (aimdContextCreateInfo::enableGpu):
 *   1. After waiting for the frame's fence and beginning its command buffer, call aimdGpuBeginFrame(). It returns a
 *      bindless handle to pass to your shaders (e.g. through push constants).
 *   2. In compute shaders recorded on that command buffer, #include "Shaders/AIMDDebug.hlsli" and draw:
 *          AIMDDebugRenderer renderer = AIMDDebugRenderer::Create(handle);
 *          renderer.SetColor(AIMDColor(float4(0, 1, 0, 1)));
 *          renderer.DrawBox(transform);
 *   3. aimdExecute() draws the GPU output together with the CPU batches, through indirect draws.
 *
 * Conventions:
 *   - Matrices are 16 floats, column-major, column vectors (glm layout): clip = P * V * world.
 *   - Clip-space depth is [0, 1]. Reverse-Z is supported through aimdExecuteInfo::reverseZ.
 *   - Colors are packed RGBA8, R in the lowest byte (same as ImGui's IM_COL32). Build them with AIMD_RGBA.
 *   - Line thickness and point size are in pixels.
 */

#pragma once

#include <stdint.h>
#include <agfx/agfx.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t aimdBool;
typedef struct aimdContext aimdContext;

#define AIMD_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

typedef struct aimdVec3 {
    float x, y, z;
} aimdVec3;

//
// Context
//

/// @brief Parameters for aimdContextCreate.
typedef struct aimdContextCreateInfo {
    agfxDevice* device;
    /// @brief Number of frames that may be in flight at once. aimdExecuteInfo::frameIndex must be < this.
    uint32_t framesInFlight;
    /// @brief Modules compiled from Shaders/AIMD.hlsl. AIMD takes ownership and destroys them in aimdContextDestroy.
    agfxShaderModule* lineVertexShader;     // LineVS
    agfxShaderModule* pointVertexShader;    // PointVS
    agfxShaderModule* triangleVertexShader; // TriangleVS
    agfxShaderModule* fragmentShader;       // MainPS
    /// @brief Initial capacity of each GPU primitive buffer, in primitives. 0 picks a default. Buffers grow on demand.
    uint32_t initialCapacity;

    /// @brief Enables the GPU-driven path (aimdGpuBeginFrame + Shaders/AIMDDebug.hlsli).
    aimdBool enableGpu;
    /// @brief FinalizeCS from Shaders/AIMD.hlsl. Required when enableGpu is set. AIMD takes ownership.
    agfxShaderModule* finalizeComputeShader;
    /// @brief Initial capacity of each GPU-written region (triangle vertices, lines or points, per depth bucket).
    ///        0 picks a default. Shapes that don't fit are dropped.
    uint32_t gpuInitialCapacity;
    /// @brief Reads the GPU counters back every frame (a few bytes) and grows the regions when shapes were dropped.
    ///        Growth takes effect framesInFlight frames after the overflow.
    aimdBool gpuAutoGrow;
    /// @brief Upper bound for auto-grow, per region, in slots of 32 bytes. 0 picks a default (2M slots, 64 MB).
    uint32_t gpuMaxCapacity;
} aimdContextCreateInfo;

/// @brief Creates a context and makes it current.
aimdContext* aimdContextCreate(const aimdContextCreateInfo* createInfo);
/// @brief Destroys a context. The GPU must no longer be using any frame it rendered (e.g. agfxDeviceWaitIdle first).
void aimdContextDestroy(aimdContext* context);
void aimdSetCurrentContext(aimdContext* context);
aimdContext* aimdGetCurrentContext(void);

//
// Style
//

typedef enum aimdStyleFlags {
    AIMD_STYLE_NONE             = 0,
    /// @brief Shapes are drawn as solid triangles instead of wireframe.
    AIMD_STYLE_FILLED           = 1 << 0,
    /// @brief Ignore the depth target; draw on top of everything.
    AIMD_STYLE_NO_DEPTH_TEST    = 1 << 1,
    /// @brief Points are drawn as discs instead of squares.
    AIMD_STYLE_ROUND_POINTS     = 1 << 2,
    /// @brief aimdStyle::textSize is in pixels instead of world units (labels no longer shrink with distance).
    AIMD_STYLE_TEXT_PIXEL_SIZE  = 1 << 3,
    /// @brief Filled shapes get basic lighting (key light + hemisphere ambient) instead of a flat color.
    ///        Round shapes are smooth shaded, flat ones use their face normal.
    AIMD_STYLE_SHADED           = 1 << 4
} aimdStyleFlags;

typedef struct aimdStyle {
    uint32_t color;     // AIMD_RGBA
    float thickness;    // Line thickness in pixels
    float pointSize;    // Point diameter in pixels
    uint32_t segments;  // Tessellation of round shapes (circles, spheres, cones, cylinders). Clamped to [3, 256].
    float textSize;     // Text height, in world units or pixels (see AIMD_STYLE_TEXT_PIXEL_SIZE)
    uint32_t flags;     // aimdStyleFlags
} aimdStyle;

/// @brief Default style: opaque white, 2px lines, 6px points, 24 segments, 0.25 world-unit text, wireframe, depth tested.
aimdStyle aimdDefaultStyle(void);

/// @brief Pushes a copy of the given style, making it current until the matching aimdPopStyle.
void aimdPushStyle(const aimdStyle* style);
void aimdPopStyle(void);
/// @brief Returns the current style. Modifications through the pointer apply to the current stack entry.
aimdStyle* aimdGetStyle(void);

// Shortcuts that modify the current stack entry.
void aimdSetColor(uint32_t color);
void aimdSetThickness(float thickness);
void aimdSetPointSize(float pointSize);
void aimdSetFilled(aimdBool filled);
void aimdSetDepthTest(aimdBool depthTest);

//
// Primitives
//

void aimdLine(aimdVec3 a, aimdVec3 b);
void aimdPolyline(const aimdVec3* points, uint32_t count, aimdBool closed);
void aimdPoint(aimdVec3 p);
/// @brief Filled triangle when AIMD_STYLE_FILLED is set, outline otherwise.
void aimdTriangle(aimdVec3 a, aimdVec3 b, aimdVec3 c);
/// @brief Quad with corners in winding order. Filled or outlined like aimdTriangle.
void aimdQuad(aimdVec3 a, aimdVec3 b, aimdVec3 c, aimdVec3 d);

//
// Shapes
//

// Closed shapes (box, AABB, frustum, cone, cylinder, sphere, arrow heads) only draw their camera-facing side when filled,
// regardless of winding order. Open ones (triangle, quad, circle, ring) are two-sided.

/// @brief Unit cube ([-0.5, 0.5]^3) transformed by the given matrix (oriented box).
void aimdBox(const float transform[16]);
void aimdAABB(aimdVec3 min, aimdVec3 max);
/// @brief Frustum whose corners are the NDC cube ([-1,1]x[-1,1]x[0,1]) transformed by invViewProjection.
void aimdFrustum(const float invViewProjection[16]);
void aimdCone(aimdVec3 apex, aimdVec3 baseCenter, float radius);
void aimdCylinder(aimdVec3 a, aimdVec3 b, float radius);
/// @brief Line from 'from' to 'to' with a cone head of length headSize. Only the head is filled with AIMD_STYLE_FILLED.
void aimdArrow(aimdVec3 from, aimdVec3 to, float headSize);
/// @brief X/Y/Z arrows (red/green/blue) of the given length along the transform's basis. Ignores the style color.
void aimdAxes(const float transform[16], float size);
void aimdCircle(aimdVec3 center, aimdVec3 normal, float radius);
/// @brief Annulus between two radii. Outlined as two circles when not filled.
void aimdRing(aimdVec3 center, aimdVec3 normal, float innerRadius, float outerRadius);
/// @brief Three axis-aligned circles (a rotation-gizmo look), colored X/Y/Z. Always outlined; ignores the style color.
void aimdRings(aimdVec3 center, float radius);
/// @brief UV sphere with segments / 2 rings and 'segments' sectors (see aimdStyle::segments).
void aimdSphere(aimdVec3 center, float radius);
/// @brief UV sphere with explicit tessellation: 'rings' latitude bands (>= 2) and 'sectors' longitude slices (>= 3).
///        The wireframe draws exactly the tessellation's edges.
void aimdSphereEx(aimdVec3 center, float radius, uint32_t rings, uint32_t sectors);
/// @brief Square grid of cells x cells, of total width 'size', lying in the plane with the given normal. Always lines.
void aimdGrid(aimdVec3 center, aimdVec3 normal, float size, uint32_t cells);

//
// Text
//

/// @brief Queues a text label anchored at a 3D position. AIMD does not draw glyphs itself: at execute time it projects
///        the anchor and passes an aimdTextCommand to aimdExecuteInfo::textCallback, which draws with any UI library.
void aimdText(aimdVec3 position, const char* text);
void aimdTextf(aimdVec3 position, const char* format, ...);

typedef struct aimdTextCommand {
    const char* text;   // Valid until the callback returns
    float x, y;         // Anchor in pixels, top-left origin. Center the text on it.
    float pixelHeight;  // Font size to draw with
    float depth;        // Clip-space w (view distance), for sorting or fading
    uint32_t color;     // AIMD_RGBA
} aimdTextCommand;

/// @brief Receives every visible label of the frame, sorted back to front.
typedef void (*aimdTextCallback)(const aimdTextCommand* commands, uint32_t count, void* userData);

//
// Execution
//

typedef struct aimdExecuteInfo {
    /// @brief Command buffer to record into. AIMD begins and ends its own render pass on it.
    agfxCommandBuffer* commandBuffer;
    /// @brief Must be in AGFX_RESOURCE_STATE_RENDER_TARGET. Contents are loaded, not cleared.
    agfxRenderTarget* colorTarget;
    agfxTextureFormat colorFormat;
    /// @brief Optional. Must be in AGFX_RESOURCE_STATE_DEPTH_WRITE. Without it, everything draws without depth testing.
    agfxRenderTarget* depthTarget;
    agfxTextureFormat depthFormat;
    uint32_t width, height;
    float view[16];
    float projection[16];
    /// @brief Depth compare becomes GREATER_EQUAL instead of LESS_EQUAL.
    aimdBool reverseZ;
    /// @brief Whether debug geometry writes depth. Off by default so it doesn't disturb the scene.
    ///        GPU-driven primitives always write depth: they are appended in a nondeterministic order, so overlaps
    ///        would flicker otherwise. (Translucent GPU shapes still blend in that order.)
    aimdBool depthWrite;
    /// @brief Index of the frame in flight, in [0, framesInFlight). Selects which upload buffers are written.
    uint32_t frameIndex;
    /// @brief Optional. Called once with every visible label, after rendering.
    aimdTextCallback textCallback;
    void* textUserData;
} aimdExecuteInfo;

/// @brief Renders everything queued since the last execute, then clears the batches.
void aimdExecute(const aimdExecuteInfo* executeInfo);

typedef struct aimdStats {
    uint32_t lines;     // Line segments
    uint32_t points;
    uint32_t triangles;
    uint32_t texts;     // Queued labels
    uint32_t drawCalls;

    // GPU-driven path, from the latest counter readback (framesInFlight frames old). Zero without gpuAutoGrow.
    uint32_t gpuLines;      // Requested, including dropped ones
    uint32_t gpuPoints;
    uint32_t gpuTriangles;
    aimdBool gpuOverflow;   // Some shapes did not fit and were dropped; the regions are growing
} aimdStats;

/// @brief Stats of the last aimdExecute.
void aimdGetStats(aimdStats* stats);

//
// GPU-driven rendering
//

/// @brief Starts a GPU-driven frame: applies pending growth, resets the GPU counters (recorded on commandBuffer) and
///        returns the handle to pass to AIMDDebugRenderer::Create. Returns 0xFFFFFFFF if GPU mode is disabled.
/// @note  Call it after waiting for frameIndex's previous submission, on the command buffer that will also receive
///        your shape-emitting compute passes and then aimdExecute (same frameIndex). No barriers are needed around
///        your compute passes: AIMD records them.
uint32_t aimdGpuBeginFrame(agfxCommandBuffer* commandBuffer, uint32_t frameIndex);

#ifdef __cplusplus
}
#endif
