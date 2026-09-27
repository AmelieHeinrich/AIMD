/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * Shader for ThirdParty/agfx_imgui. Entry points: VSMain, PSMain.
 * Push constants must match ImGui_ImplAGFX_PushConstants in imgui_impl_agfx.cpp.
 */

#if defined(AGFX_VULKAN)
    #define IMGUI_PUSH_CONSTANTS(type, name) [[vk::push_constant]] ConstantBuffer<type> name : register(b0)
#else
    #define IMGUI_PUSH_CONSTANTS(type, name) ConstantBuffer<type> name : register(b0)
#endif

struct PushConstants {
    float2 scale;
    float2 translate;
    uint vertexOffset;
    uint vertexBuffer;
    uint texture;
    uint textureSampler;
};
IMGUI_PUSH_CONSTANTS(PushConstants, PC);

struct ImDrawVert {
    float2 pos;
    float2 uv;
    uint col;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VertexOut VSMain(uint vertexID : SV_VertexID) {
    StructuredBuffer<ImDrawVert> vertices = ResourceDescriptorHeap[PC.vertexBuffer];
    ImDrawVert v = vertices[PC.vertexOffset + vertexID];

    VertexOut o;
    o.position = float4(v.pos * PC.scale + PC.translate, 0.0, 1.0);
    o.color = float4(v.col & 0xFF, (v.col >> 8) & 0xFF, (v.col >> 16) & 0xFF, (v.col >> 24) & 0xFF) / 255.0;
    o.uv = v.uv;
    return o;
}

float4 PSMain(VertexOut input) : SV_Target0 {
    Texture2D<float4> texture = ResourceDescriptorHeap[PC.texture];
    SamplerState textureSampler = SamplerDescriptorHeap[PC.textureSampler];
    return input.color * texture.Sample(textureSampler, input.uv);
}
