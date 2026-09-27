/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 */

#include "Platform.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_agfx.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

static void* PlatformAllocate(uint64_t size, void*) { return malloc(size); }
static void PlatformFree(void* ptr, void*) { free(ptr); }

static void PlatformLog(agfxLogSeverity severity, const char* message) {
    const char* prefix = severity == AGFX_LOG_SEVERITY_ERROR ? "error" : severity == AGFX_LOG_SEVERITY_WARNING ? "warning" : "info";
    fprintf(stderr, "[agfx %s] %s\n", prefix, message);
}

//
// Lifetime
//

bool Platform::Init(const char* title, uint32_t width, uint32_t height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__APPLE__)
    windowFlags |= SDL_WINDOW_METAL;
#elif defined(__linux__)
    windowFlags |= SDL_WINDOW_VULKAN;
#endif
    mWindow = SDL_CreateWindow(title, (int)width, (int)height, windowFlags);
    if (!mWindow) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    agfxDeviceCreateInfo deviceInfo = {};
    deviceInfo.allocate = PlatformAllocate;
    deviceInfo.free = PlatformFree;
    deviceInfo.tempAllocate = PlatformAllocate;
    deviceInfo.tempFree = PlatformFree;
    deviceInfo.logFunction = PlatformLog;
    deviceInfo.enableValidation = 1;

    // Native window handle for the swap chain
    SDL_PropertiesID props = SDL_GetWindowProperties(mWindow);
#if defined(__APPLE__)
    mMetalView = SDL_Metal_CreateView(mWindow);
    mWindowHandle = SDL_Metal_GetLayer(mMetalView);
#elif defined(_WIN32)
    mWindowHandle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        deviceInfo.displayServerProtocol = AGFX_DISPLAY_SERVER_PROTOCOL_WAYLAND;
        mLinuxHandle.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        mLinuxHandle.window = (uint64_t)(uintptr_t)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        deviceInfo.displayServerProtocol = AGFX_DISPLAY_SERVER_PROTOCOL_X11;
        mLinuxHandle.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        mLinuxHandle.window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    }
    mWindowHandle = &mLinuxHandle;
#endif
    (void)props;

    mDevice = agfxDeviceCreate(&deviceInfo);
    if (!mDevice) {
        fprintf(stderr, "agfxDeviceCreate failed\n");
        return false;
    }

    agfxCommandQueueCreateInfo queueInfo = {};
    queueInfo.type = AGFX_COMMAND_QUEUE_TYPE_GRAPHICS;
    mQueue = agfxCommandQueueCreate(mDevice, &queueInfo);
    for (agfxCommandBuffer*& commandBuffer : mCommandBuffers)
        commandBuffer = agfxCommandBufferCreate(mDevice, mQueue);
    mFence = agfxFenceCreate(mDevice);

    int pixelWidth = 0, pixelHeight = 0;
    SDL_GetWindowSizeInPixels(mWindow, &pixelWidth, &pixelHeight);
    if (!CreateSurface((uint32_t)pixelWidth, (uint32_t)pixelHeight)) {
        fprintf(stderr, "Failed to create the swap chain\n");
        return false;
    }

    // ImGui
    ImGui_ImplAGFX_InitInfo imguiInfo = {};
    imguiInfo.Device = mDevice;
    imguiInfo.CommandQueue = mQueue;
    imguiInfo.ColorAttachmentFormat = mColorFormat;
    imguiInfo.VertexShaderModule = CompileShader("Demo/Shaders/ImGui.hlsl", "VSMain", AGFX_SHADER_STAGE_VERTEX);
    imguiInfo.FragmentShaderModule = CompileShader("Demo/Shaders/ImGui.hlsl", "PSMain", AGFX_SHADER_STAGE_FRAGMENT);
    imguiInfo.FramesInFlight = kFramesInFlight;
    if (!imguiInfo.VertexShaderModule || !imguiInfo.FragmentShaderModule)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOther(mWindow);
    ImGui_ImplAGFX_Init(&imguiInfo);

    mLastTicks = SDL_GetTicksNS();
    return true;
}

void Platform::Shutdown() {
    if (mDevice)
        agfxDeviceWaitIdle(mDevice);

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplAGFX_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    if (mDevice) {
        DestroySurface();
        if (mFence)
            agfxFenceDestroy(mDevice, mFence);
        for (agfxCommandBuffer* commandBuffer : mCommandBuffers)
            if (commandBuffer)
                agfxCommandBufferDestroy(mDevice, commandBuffer);
        if (mQueue)
            agfxCommandQueueDestroy(mDevice, mQueue);
        agfxDeviceDestroy(mDevice);
    }

#if defined(__APPLE__)
    if (mMetalView)
        SDL_Metal_DestroyView(mMetalView);
#endif
    if (mWindow)
        SDL_DestroyWindow(mWindow);
    SDL_Quit();
}

//
// Swap chain and depth
//

bool Platform::CreateSurface(uint32_t width, uint32_t height) {
    agfxSwapChainCreateInfo swapInfo = {};
    swapInfo.queue = mQueue;
    swapInfo.imageCount = kFramesInFlight;
    swapInfo.width = width;
    swapInfo.height = height;
    swapInfo.vsync = 1;
    swapInfo.handle = mWindowHandle;
    mSwapChain = agfxSwapChainCreate(mDevice, &swapInfo);
    if (!mSwapChain)
        return false;
    mColorFormat = agfxSwapChainGetFormat(mSwapChain);

    agfxTextureCreateInfo depthInfo = {};
    depthInfo.type = AGFX_TEXTURE_TYPE_2D;
    depthInfo.format = kDepthFormat;
    depthInfo.usage = AGFX_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT;
    depthInfo.width = width;
    depthInfo.height = height;
    depthInfo.depthOrArrayLayers = 1;
    depthInfo.mipLevels = 1;
    mDepth = agfxTextureCreate(mDevice, &depthInfo);
    if (!mDepth)
        return false;
    agfxTextureSetName(mDepth, "Demo Depth");

    agfxRenderTargetCreateInfo rtInfo = {};
    rtInfo.texture = mDepth;
    rtInfo.format = kDepthFormat;
    rtInfo.isDepth = 1;
    mDepthTarget = agfxRenderTargetCreate(mDevice, &rtInfo);

    mDepthNeedsTransition = true;
    mWidth = width;
    mHeight = height;
    return mDepthTarget != nullptr;
}

void Platform::DestroySurface() {
    for (agfxRenderTarget*& target : mColorTargets) {
        if (target)
            agfxRenderTargetDestroy(mDevice, target);
        target = nullptr;
    }
    if (mDepthTarget)
        agfxRenderTargetDestroy(mDevice, mDepthTarget);
    if (mDepth)
        agfxTextureDestroy(mDevice, mDepth);
    if (mSwapChain)
        agfxSwapChainDestroy(mDevice, mSwapChain);
    mDepthTarget = nullptr;
    mDepth = nullptr;
    mSwapChain = nullptr;
}

//
// Frame
//

bool Platform::PollEvents(PlatformInput& input) {
    input = {};
    ImGuiIO& io = ImGui::GetIO();
    bool resized = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return false;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            resized = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_RIGHT && !io.WantCaptureMouse) {
                mLooking = true;
                SDL_SetWindowRelativeMouseMode(mWindow, true);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_RIGHT && mLooking) {
                mLooking = false;
                SDL_SetWindowRelativeMouseMode(mWindow, false);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (mLooking) {
                input.lookX += event.motion.xrel;
                input.lookY += event.motion.yrel;
            }
            break;
        default:
            break;
        }
    }

    if (resized) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(mWindow, &width, &height);
        if (width > 0 && height > 0 && ((uint32_t)width != mWidth || (uint32_t)height != mHeight)) {
            agfxDeviceWaitIdle(mDevice);
            DestroySurface();
            if (!CreateSurface((uint32_t)width, (uint32_t)height)) {
                fprintf(stderr, "Failed to recreate the swap chain\n");
                return false;
            }
        }
    }

    if (!io.WantCaptureKeyboard) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        input.forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
        input.right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
        input.up = (keys[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
        input.fast = keys[SDL_SCANCODE_LSHIFT];
    }
    return true;
}

bool Platform::BeginFrame(PlatformFrame& frame) {
    uint64_t ticks = SDL_GetTicksNS();
    frame.deltaTime = (float)(ticks - mLastTicks) * 1e-9f;
    mLastTicks = ticks;
    mTime += frame.deltaTime;
    frame.time = mTime;

    if (SDL_GetWindowFlags(mWindow) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(16);
        return false;
    }

    // Wait until this frame slot's previous submission has finished
    frame.index = (uint32_t)(mFrameCounter % kFramesInFlight);
    agfxFenceWait(mFence, mFrameFenceValues[frame.index], UINT64_MAX);
    if (mColorTargets[frame.index]) {
        agfxRenderTargetDestroy(mDevice, mColorTargets[frame.index]);
        mColorTargets[frame.index] = nullptr;
    }

    // Acquire before recording anything: nextDrawable can time out and return null (e.g. while the window is
    // occluded or on another Space). Skip the frame in that case; nothing has been started yet.
    mBackBuffer = agfxSwapChainAcquireNextTexture(mSwapChain);
    if (!mBackBuffer)
        return false;

    agfxRenderTargetCreateInfo rtInfo = {};
    rtInfo.texture = mBackBuffer;
    rtInfo.format = AGFX_TEXTURE_FORMAT_UNKNOWN;
    mColorTargets[frame.index] = agfxRenderTargetCreate(mDevice, &rtInfo);
    frame.colorTarget = mColorTargets[frame.index];

    frame.commandBuffer = mCommandBuffers[frame.index];
    agfxCommandBufferReset(frame.commandBuffer);
    agfxCommandBufferBegin(frame.commandBuffer);
    agfxCommandBufferTextureBarrier(frame.commandBuffer, mBackBuffer, AGFX_RESOURCE_STATE_PRESENT, AGFX_RESOURCE_STATE_RENDER_TARGET,
                                    AGFX_SUBRESOURCE_ALL_MIPS, AGFX_SUBRESOURCE_ALL_LAYERS, 0);
    if (mDepthNeedsTransition) {
        agfxCommandBufferTextureBarrier(frame.commandBuffer, mDepth, AGFX_RESOURCE_STATE_COMMON, AGFX_RESOURCE_STATE_DEPTH_WRITE,
                                        AGFX_SUBRESOURCE_ALL_MIPS, AGFX_SUBRESOURCE_ALL_LAYERS, 1);
        mDepthNeedsTransition = false;
    }

    ImGui_ImplAGFX_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Platform::EndFrame(const PlatformFrame& frame) {
    agfxCommandBuffer* cmd = frame.commandBuffer;

    ImGui::Render();
    agfxRenderPassCreateInfo passInfo = {};
    passInfo.name = "ImGui";
    passInfo.width = mWidth;
    passInfo.height = mHeight;
    passInfo.colorAttachmentCount = 1;
    passInfo.colorAttachments[0].renderTarget = frame.colorTarget;
    passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_LOAD;
    passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
    agfxRenderPass* pass = agfxRenderPassBegin(cmd, &passInfo);
    ImGui_ImplAGFX_RenderDrawData(ImGui::GetDrawData(), pass, mWidth, mHeight, frame.index);
    agfxRenderPassEnd(pass);

    agfxCommandBufferTextureBarrier(cmd, mBackBuffer, AGFX_RESOURCE_STATE_RENDER_TARGET, AGFX_RESOURCE_STATE_PRESENT,
                                    AGFX_SUBRESOURCE_ALL_MIPS, AGFX_SUBRESOURCE_ALL_LAYERS, 0);
    agfxCommandBufferEnd(cmd);
    agfxCommandQueueSubmit(mQueue, &cmd, 1);
    mFrameFenceValues[frame.index] = ++mFenceValue;
    agfxCommandQueueSignal(mQueue, mFence, mFenceValue);
    agfxSwapChainPresent(mSwapChain);
    mFrameCounter++;
}

//
// Shaders
//

agfxShaderModule* Platform::CompileShader(const char* path, const char* entryPoint, agfxShaderStage stage, uint32_t groupSize[3]) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fprintf(stderr, "Could not read %s. Run the demo from the project root (xmake run does).\n", path);
        return nullptr;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    agfxShaderCompilerOptions options = {};
    options.stage = stage;
    snprintf(options.entryPoint, sizeof(options.entryPoint), "%s", entryPoint);
    options.sourceCode = source.data();
    options.sourceCodeSize = (uint32_t)source.size();
    options.dxCompilerPath = "Binaries/libdxcompiler.so"; // Linux only, relative to the run directory
    // Without this, GPU-Based Validation can only report a raw descriptor index and shader stage, not
    // the HLSL source line or variable name behind it.
    options.addDebugSymbols = 1;

    agfxShaderCompilerResult result = {};
    agfxCompileShader(&options, &result);
    if (!result.compiledCode) {
        fprintf(stderr, "Failed to compile %s (%s)\n", path, entryPoint);
        return nullptr;
    }

    agfxShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.code = result.compiledCode;
    moduleInfo.codeSize = result.compiledSize;
    moduleInfo.entryPoint = entryPoint; // Must outlive the module: pass string literals
    moduleInfo.type = stage == AGFX_SHADER_STAGE_VERTEX   ? AGFX_SHADER_MODULE_TYPE_VERTEX
                    : stage == AGFX_SHADER_STAGE_FRAGMENT ? AGFX_SHADER_MODULE_TYPE_FRAGMENT
                                                          : AGFX_SHADER_MODULE_TYPE_COMPUTE;
    agfxShaderModule* module = agfxShaderModuleCreate(mDevice, &moduleInfo);
    free(result.compiledCode);
    if (groupSize) {
        groupSize[0] = result.tgSizeX;
        groupSize[1] = result.tgSizeY;
        groupSize[2] = result.tgSizeZ;
    }
    return module;
}
