/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * Everything the demo needs that isn't AIMD: SDL3 window and input, agfx device, swap chain, depth buffer, frame pacing
 * and ImGui. Nothing in here is required to use AIMD.
 */

#pragma once

#include <agfx/agfx.h>
#include <agfx_shader/agfx_shader_compiler.h>
#include <stdint.h>

struct SDL_Window;

struct PlatformInput {
    float lookX = 0.0f, lookY = 0.0f;               // Mouse motion while the right button is held
    float forward = 0.0f, right = 0.0f, up = 0.0f;  // WASD + Q/E, in [-1, 1]
    bool fast = false;                              // Shift
};

struct PlatformFrame {
    agfxCommandBuffer* commandBuffer = nullptr;
    agfxRenderTarget* colorTarget = nullptr;        // Back buffer, in RENDER_TARGET state
    uint32_t index = 0;                             // Frame in flight, in [0, Platform::kFramesInFlight)
    float deltaTime = 0.0f;                         // Seconds since the previous BeginFrame
    float time = 0.0f;                              // Seconds since Init
};

class Platform {
public:
    static constexpr uint32_t kFramesInFlight = 3;
    static constexpr agfxTextureFormat kDepthFormat = AGFX_TEXTURE_FORMAT_DEPTH32F;

    bool Init(const char* title, uint32_t width, uint32_t height);
    void Shutdown();

    /// @brief Pumps events and handles resizes. Returns false once the window is closed.
    bool PollEvents(PlatformInput& input);
    /// @brief Waits for the frame slot, acquires the back buffer, begins the command buffer and a new ImGui frame.
    ///        Returns false when the frame must be skipped (minimized window, no drawable).
    bool BeginFrame(PlatformFrame& frame);
    /// @brief Draws ImGui over the back buffer, then submits and presents.
    void EndFrame(const PlatformFrame& frame);

    /// @brief Compiles one entry point of an HLSL file (path relative to the project root) into a shader module.
    agfxShaderModule* CompileShader(const char* path, const char* entryPoint, agfxShaderStage stage, uint32_t groupSize[3] = nullptr);

    agfxDevice* Device() const { return mDevice; }
    agfxTextureFormat ColorFormat() const { return mColorFormat; }
    agfxTexture* Depth() const { return mDepth; }
    agfxRenderTarget* DepthTarget() const { return mDepthTarget; }
    uint32_t Width() const { return mWidth; }
    uint32_t Height() const { return mHeight; }

private:
    bool CreateSurface(uint32_t width, uint32_t height);
    void DestroySurface();

    SDL_Window* mWindow = nullptr;
    void* mMetalView = nullptr;
    void* mWindowHandle = nullptr;                  // HWND, CAMetalLayer* or agfxLinuxWindowHandle*
#if defined(__linux__)
    agfxLinuxWindowHandle mLinuxHandle = {};
#endif
    bool mLooking = false;
    uint64_t mLastTicks = 0;
    float mTime = 0.0f;

    agfxDevice* mDevice = nullptr;
    agfxCommandQueue* mQueue = nullptr;
    agfxCommandBuffer* mCommandBuffers[kFramesInFlight] = {};
    agfxFence* mFence = nullptr;
    uint64_t mFenceValue = 0;
    uint64_t mFrameFenceValues[kFramesInFlight] = {};
    uint64_t mFrameCounter = 0;

    // Swap chain and depth, recreated on resize
    agfxSwapChain* mSwapChain = nullptr;
    agfxTextureFormat mColorFormat = AGFX_TEXTURE_FORMAT_UNKNOWN;
    agfxTexture* mBackBuffer = nullptr;
    agfxRenderTarget* mColorTargets[kFramesInFlight] = {};
    agfxTexture* mDepth = nullptr;
    agfxRenderTarget* mDepthTarget = nullptr;
    bool mDepthNeedsTransition = false;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
};
