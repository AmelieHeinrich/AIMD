/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * AIMD demo: SDL3 window, agfx renderer, first person camera and an ImGui panel to style the shapes.
 */

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_agfx.h>

#include <agfx/agfx.h>
#include <agfx_shader/agfx_shader_compiler.h>
#include <aimd/aimd.h>

#include <glm/gtc/type_ptr.hpp>

#include "Camera.h"
#include "Showcase.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static constexpr uint32_t kFramesInFlight = 3;

//
// Helpers
//

static void* DemoAllocate(uint64_t size, void*) { return malloc(size); }
static void DemoFree(void* ptr, void*) { free(ptr); }

static void DemoLog(agfxLogSeverity severity, const char* message) {
    const char* prefix = severity == AGFX_LOG_SEVERITY_ERROR ? "error" : severity == AGFX_LOG_SEVERITY_WARNING ? "warning" : "info";
    fprintf(stderr, "[agfx %s] %s\n", prefix, message);
}

static std::string ReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static agfxShaderModule* CompileShader(agfxDevice* device, const std::string& source, const char* entryPoint, agfxShaderStage stage,
                                       uint32_t* groupSize = nullptr) {
    agfxShaderCompilerOptions options = {};
    options.stage = stage;
    snprintf(options.entryPoint, sizeof(options.entryPoint), "%s", entryPoint);
    options.sourceCode = (char*)source.data();
    options.sourceCodeSize = (uint32_t)source.size();
    options.dxCompilerPath = "Binaries/libdxcompiler.so"; // Linux only, relative to the run directory
    // Without this, GPU-Based Validation can only report a raw descriptor index and shader stage, not
    // the HLSL source line or variable name behind it.
    options.addDebugSymbols = 1;

    agfxShaderCompilerResult result = {};
    agfxCompileShader(&options, &result);
    if (!result.compiledCode) {
        fprintf(stderr, "Failed to compile shader entry point %s\n", entryPoint);
        return nullptr;
    }

    agfxShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.code = result.compiledCode;
    moduleInfo.codeSize = result.compiledSize;
    moduleInfo.entryPoint = entryPoint; // String literals only: must outlive the module
    moduleInfo.type = stage == AGFX_SHADER_STAGE_VERTEX   ? AGFX_SHADER_MODULE_TYPE_VERTEX
                    : stage == AGFX_SHADER_STAGE_FRAGMENT ? AGFX_SHADER_MODULE_TYPE_FRAGMENT
                                                          : AGFX_SHADER_MODULE_TYPE_COMPUTE;
    agfxShaderModule* module = agfxShaderModuleCreate(device, &moduleInfo);
    free(result.compiledCode);
    if (groupSize) {
        groupSize[0] = result.tgSizeX;
        groupSize[1] = result.tgSizeY;
        groupSize[2] = result.tgSizeZ;
    }
    return module;
}

// Draws AIMD's projected labels with ImGui. AIMD works in framebuffer pixels, ImGui in window points.
static void DrawTextWithImGui(const aimdTextCommand* commands, uint32_t count, void*) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float invScale = scale.x > 0.0f ? 1.0f / scale.x : 1.0f;

    for (uint32_t i = 0; i < count; ++i) {
        const aimdTextCommand& cmd = commands[i];
        float size = cmd.pixelHeight * invScale;
        ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, cmd.text);
        ImVec2 pos(cmd.x * invScale - extent.x * 0.5f, cmd.y * invScale - extent.y * 0.5f);
        uint32_t shadowAlpha = (cmd.color >> 24) * 3 / 4;
        drawList->AddText(font, size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, shadowAlpha), cmd.text);
        drawList->AddText(font, size, pos, cmd.color, cmd.text);
    }
}

// Must match SceneConstants in Shaders/DemoGpuScene.hlsl
struct SceneConstants {
    float cullViewProjection[16];
    uint32_t aimdHandle;
    uint32_t instanceCount;
    float time;
    uint32_t flags;
    uint32_t sphereRings;
    uint32_t sphereSectors;
    float thickness;
    uint32_t pad;
};

enum SceneFlags : uint32_t {
    SCENE_SHOW_CULLED = 1 << 0,
    SCENE_FILLED = 1 << 1,
    SCENE_SHADED = 1 << 2,
    SCENE_SPHERES = 1 << 3,
    SCENE_XRAY = 1 << 4,
};

//
// Swap chain and depth
//

struct Surface {
    agfxSwapChain* swapChain = nullptr;
    agfxTextureFormat colorFormat = AGFX_TEXTURE_FORMAT_UNKNOWN;
    agfxTexture* depth = nullptr;
    agfxRenderTarget* depthTarget = nullptr;
    bool depthNeedsTransition = false;
    uint32_t width = 0;
    uint32_t height = 0;
};

static void DestroySurface(agfxDevice* device, Surface& surface) {
    if (surface.depthTarget)
        agfxRenderTargetDestroy(device, surface.depthTarget);
    if (surface.depth)
        agfxTextureDestroy(device, surface.depth);
    if (surface.swapChain)
        agfxSwapChainDestroy(device, surface.swapChain);
    surface = {};
}

static bool CreateSurface(agfxDevice* device, agfxCommandQueue* queue, void* windowHandle, uint32_t width, uint32_t height, Surface& surface) {
    agfxSwapChainCreateInfo swapInfo = {};
    swapInfo.queue = queue;
    swapInfo.imageCount = kFramesInFlight;
    swapInfo.width = width;
    swapInfo.height = height;
    swapInfo.vsync = 1;
    swapInfo.handle = windowHandle;
    surface.swapChain = agfxSwapChainCreate(device, &swapInfo);
    if (!surface.swapChain)
        return false;
    surface.colorFormat = agfxSwapChainGetFormat(surface.swapChain);

    agfxTextureCreateInfo depthInfo = {};
    depthInfo.type = AGFX_TEXTURE_TYPE_2D;
    depthInfo.format = AGFX_TEXTURE_FORMAT_DEPTH32F;
    depthInfo.usage = AGFX_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT;
    depthInfo.width = width;
    depthInfo.height = height;
    depthInfo.depthOrArrayLayers = 1;
    depthInfo.mipLevels = 1;
    surface.depth = agfxTextureCreate(device, &depthInfo);
    if (!surface.depth)
        return false;
    agfxTextureSetName(surface.depth, "Demo Depth");

    agfxRenderTargetCreateInfo rtInfo = {};
    rtInfo.texture = surface.depth;
    rtInfo.format = AGFX_TEXTURE_FORMAT_DEPTH32F;
    rtInfo.isDepth = 1;
    surface.depthTarget = agfxRenderTargetCreate(device, &rtInfo);

    surface.depthNeedsTransition = true;
    surface.width = width;
    surface.height = height;
    return surface.depthTarget != nullptr;
}

//
// Main
//

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__APPLE__)
    windowFlags |= SDL_WINDOW_METAL;
#elif defined(__linux__)
    windowFlags |= SDL_WINDOW_VULKAN;
#endif
    SDL_Window* window = SDL_CreateWindow("AIMD Demo", 1600, 900, windowFlags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    // Device
    agfxDeviceCreateInfo deviceInfo = {};
    deviceInfo.allocate = DemoAllocate;
    deviceInfo.free = DemoFree;
    deviceInfo.tempAllocate = DemoAllocate;
    deviceInfo.tempFree = DemoFree;
    deviceInfo.logFunction = DemoLog;
    deviceInfo.enableValidation = 1;

    // Native window handle for the swap chain
    void* windowHandle = nullptr;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
#if defined(__APPLE__)
    SDL_MetalView metalView = SDL_Metal_CreateView(window);
    windowHandle = SDL_Metal_GetLayer(metalView);
#elif defined(_WIN32)
    windowHandle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
    agfxLinuxWindowHandle linuxHandle = {};
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        deviceInfo.displayServerProtocol = AGFX_DISPLAY_SERVER_PROTOCOL_WAYLAND;
        linuxHandle.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        linuxHandle.window = (uint64_t)(uintptr_t)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        deviceInfo.displayServerProtocol = AGFX_DISPLAY_SERVER_PROTOCOL_X11;
        linuxHandle.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        linuxHandle.window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    }
    windowHandle = &linuxHandle;
#endif
    (void)props;

    agfxDevice* device = agfxDeviceCreate(&deviceInfo);
    if (!device) {
        fprintf(stderr, "agfxDeviceCreate failed\n");
        return 1;
    }

    agfxCommandQueueCreateInfo queueInfo = {};
    queueInfo.type = AGFX_COMMAND_QUEUE_TYPE_GRAPHICS;
    agfxCommandQueue* queue = agfxCommandQueueCreate(device, &queueInfo);

    agfxCommandBuffer* commandBuffers[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
        commandBuffers[i] = agfxCommandBufferCreate(device, queue);
    agfxFence* fence = agfxFenceCreate(device);
    uint64_t fenceValue = 0;
    uint64_t frameFenceValues[kFramesInFlight] = {};
    agfxRenderTarget* frameColorTargets[kFramesInFlight] = {};

    int pixelWidth = 0, pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    Surface surface;
    if (!CreateSurface(device, queue, windowHandle, (uint32_t)pixelWidth, (uint32_t)pixelHeight, surface)) {
        fprintf(stderr, "Failed to create the swap chain\n");
        return 1;
    }

    // Shaders
    std::string aimdSource = ReadFile("Shaders/AIMD.hlsl");
    std::string imguiSource = ReadFile("Shaders/ImGui.hlsl");
    std::string sceneSource = ReadFile("Shaders/DemoGpuScene.hlsl");
    if (aimdSource.empty() || imguiSource.empty() || sceneSource.empty()) {
        fprintf(stderr, "Could not read Shaders/*.hlsl. Run the demo from the project root (xmake run does).\n");
        return 1;
    }

    // AIMD
    aimdContextCreateInfo aimdInfo = {};
    aimdInfo.device = device;
    aimdInfo.framesInFlight = kFramesInFlight;
    aimdInfo.lineVertexShader = CompileShader(device, aimdSource, "LineVS", AGFX_SHADER_STAGE_VERTEX);
    aimdInfo.pointVertexShader = CompileShader(device, aimdSource, "PointVS", AGFX_SHADER_STAGE_VERTEX);
    aimdInfo.triangleVertexShader = CompileShader(device, aimdSource, "TriangleVS", AGFX_SHADER_STAGE_VERTEX);
    aimdInfo.fragmentShader = CompileShader(device, aimdSource, "MainPS", AGFX_SHADER_STAGE_FRAGMENT);
    aimdInfo.enableGpu = 1;
    aimdInfo.finalizeComputeShader = CompileShader(device, aimdSource, "FinalizeCS", AGFX_SHADER_STAGE_COMPUTE);
    aimdInfo.gpuInitialCapacity = 1024; // Deliberately small, to show auto-grow
    aimdInfo.gpuAutoGrow = 1;
    aimdContext* aimd = aimdContextCreate(&aimdInfo);
    if (!aimd) {
        fprintf(stderr, "aimdContextCreate failed\n");
        return 1;
    }

    // GPU-driven demo scene
    uint32_t sceneGroupSize[3] = {};
    agfxShaderModule* sceneShader = CompileShader(device, sceneSource, "SceneCS", AGFX_SHADER_STAGE_COMPUTE, sceneGroupSize);
    agfxComputePipelineCreateInfo scenePipelineInfo = {};
    scenePipelineInfo.name = "Demo GPU Scene";
    scenePipelineInfo.computeShader = sceneShader;
    scenePipelineInfo.groupSizeX = sceneGroupSize[0];
    scenePipelineInfo.groupSizeY = sceneGroupSize[1];
    scenePipelineInfo.groupSizeZ = sceneGroupSize[2];
    agfxComputePipeline* scenePipeline = sceneShader ? agfxComputePipelineCreate(device, &scenePipelineInfo) : nullptr;
    if (!scenePipeline) {
        fprintf(stderr, "Failed to create the GPU scene pipeline\n");
        return 1;
    }

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOther(window);

    ImGui_ImplAGFX_InitInfo imguiInfo = {};
    imguiInfo.Device = device;
    imguiInfo.CommandQueue = queue;
    imguiInfo.ColorAttachmentFormat = surface.colorFormat;
    imguiInfo.VertexShaderModule = CompileShader(device, imguiSource, "VSMain", AGFX_SHADER_STAGE_VERTEX);
    imguiInfo.FragmentShaderModule = CompileShader(device, imguiSource, "PSMain", AGFX_SHADER_STAGE_FRAGMENT);
    imguiInfo.FramesInFlight = kFramesInFlight;
    ImGui_ImplAGFX_Init(&imguiInfo);

    Camera camera;
    ShowcaseSettings settings;
    bool looking = false;
    bool running = true;
    uint64_t frameCounter = 0;
    uint64_t lastTicks = SDL_GetTicksNS();
    float time = 0.0f;
    glm::mat4 frozenCullViewProjection(1.0f);

    while (running) {
        uint64_t ticks = SDL_GetTicksNS();
        float dt = (float)(ticks - lastTicks) * 1e-9f;
        lastTicks = ticks;
        time += dt;

        // Events
        float lookX = 0.0f, lookY = 0.0f;
        bool resized = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                resized = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_RIGHT && !io.WantCaptureMouse) {
                    looking = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_RIGHT && looking) {
                    looking = false;
                    SDL_SetWindowRelativeMouseMode(window, false);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (looking) {
                    lookX += event.motion.xrel;
                    lookY += event.motion.yrel;
                }
                break;
            default:
                break;
            }
        }
        if (!running)
            break;

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(16);
            continue;
        }

        if (resized) {
            SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
            if (pixelWidth > 0 && pixelHeight > 0 && ((uint32_t)pixelWidth != surface.width || (uint32_t)pixelHeight != surface.height)) {
                agfxDeviceWaitIdle(device);
                for (agfxRenderTarget*& target : frameColorTargets) {
                    if (target)
                        agfxRenderTargetDestroy(device, target);
                    target = nullptr;
                }
                DestroySurface(device, surface);
                if (!CreateSurface(device, queue, windowHandle, (uint32_t)pixelWidth, (uint32_t)pixelHeight, surface)) {
                    fprintf(stderr, "Failed to recreate the swap chain\n");
                    break;
                }
            }
        }

        // Camera
        if (looking)
            camera.Look(lookX, lookY);
        if (!io.WantCaptureKeyboard) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
            float right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
            float up = (keys[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
            camera.Move(forward, right, up, dt, keys[SDL_SCANCODE_LSHIFT]);
        }

        // Frame pacing: wait until this frame slot's previous submission has finished
        uint32_t frameIndex = (uint32_t)(frameCounter % kFramesInFlight);
        agfxFenceWait(fence, frameFenceValues[frameIndex], UINT64_MAX);
        if (frameColorTargets[frameIndex]) {
            agfxRenderTargetDestroy(device, frameColorTargets[frameIndex]);
            frameColorTargets[frameIndex] = nullptr;
        }

        // Acquire before recording anything: nextDrawable can time out and return null (e.g. while the window is
        // occluded or on another Space). Skip the frame in that case; nothing has been started yet.
        agfxTexture* backBuffer = agfxSwapChainAcquireNextTexture(surface.swapChain);
        if (!backBuffer)
            continue;

        // UI and debug draws
        ImGui_ImplAGFX_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        aimdStats stats;
        aimdGetStats(&stats);
        ShowcaseDrawUI(settings, stats, dt);
        ShowcaseDrawScene(settings, time);

        // Culling camera of the GPU scene: finite and standard-Z so the shader can extract all six planes.
        float aspect = (float)surface.width / (float)surface.height;
        glm::mat4 view = camera.View();
        glm::mat4 cullViewProjection = glm::perspectiveRH_ZO(camera.fovY, aspect, 0.1f, 90.0f) * view;
        if (settings.gpuFreezeCulling) {
            cullViewProjection = frozenCullViewProjection;
            aimdStyle frustumStyle = aimdDefaultStyle();
            frustumStyle.color = AIMD_RGBA(255, 220, 60, 255);
            aimdPushStyle(&frustumStyle);
            aimdFrustum(glm::value_ptr(glm::inverse(cullViewProjection)));
            aimdPopStyle();
        } else {
            frozenCullViewProjection = cullViewProjection;
        }

        agfxCommandBuffer* cmd = commandBuffers[frameIndex];
        agfxCommandBufferReset(cmd);
        agfxCommandBufferBegin(cmd);

        agfxRenderTargetCreateInfo rtInfo = {};
        rtInfo.texture = backBuffer;
        rtInfo.format = AGFX_TEXTURE_FORMAT_UNKNOWN;
        agfxRenderTarget* colorTarget = agfxRenderTargetCreate(device, &rtInfo);
        frameColorTargets[frameIndex] = colorTarget;

        agfxCommandBufferTextureBarrier(cmd, backBuffer, AGFX_RESOURCE_STATE_PRESENT, AGFX_RESOURCE_STATE_RENDER_TARGET,
                                        AGFX_SUBRESOURCE_ALL_MIPS, AGFX_SUBRESOURCE_ALL_LAYERS, 0);
        if (surface.depthNeedsTransition) {
            agfxCommandBufferTextureBarrier(cmd, surface.depth, AGFX_RESOURCE_STATE_COMMON, AGFX_RESOURCE_STATE_DEPTH_WRITE,
                                            AGFX_SUBRESOURCE_ALL_MIPS, AGFX_SUBRESOURCE_ALL_LAYERS, 1);
            surface.depthNeedsTransition = false;
        }

        // GPU-driven debug shapes: AIMD resets its counters, then our compute pass appends shapes.
        uint32_t aimdHandle = aimdGpuBeginFrame(cmd, frameIndex);
        if (settings.gpuEnabled && settings.gpuInstances > 0 && aimdHandle != 0xFFFFFFFFu) {
            SceneConstants constants = {};
            memcpy(constants.cullViewProjection, glm::value_ptr(cullViewProjection), sizeof(constants.cullViewProjection));
            constants.aimdHandle = aimdHandle;
            constants.instanceCount = (uint32_t)settings.gpuInstances;
            constants.time = settings.animate ? time * settings.animationSpeed : 0.0f;
            constants.flags = (settings.gpuShowCulled ? SCENE_SHOW_CULLED : 0) | (settings.gpuFilled ? SCENE_FILLED : 0) |
                              (settings.shaded ? SCENE_SHADED : 0) | (settings.gpuSpheres ? SCENE_SPHERES : 0) |
                              (settings.gpuXray ? SCENE_XRAY : 0);
            constants.sphereRings = (uint32_t)settings.sphereRings;
            constants.sphereSectors = (uint32_t)settings.sphereSectors;
            constants.thickness = settings.thickness;

            agfxComputePass* scenePass = agfxComputePassBegin(cmd, "Demo GPU Scene");
            agfxComputePassSetPipeline(scenePass, scenePipeline);
            agfxComputePassPushConstants(scenePass, &constants, sizeof(constants));
            agfxComputePassDispatch(scenePass, (constants.instanceCount + sceneGroupSize[0] - 1) / sceneGroupSize[0], 1, 1);
            agfxComputePassEnd(scenePass);
        }

        // Clear. A real application would render its scene here.
        {
            agfxRenderPassCreateInfo passInfo = {};
            passInfo.name = "Clear";
            passInfo.width = surface.width;
            passInfo.height = surface.height;
            passInfo.colorAttachmentCount = 1;
            passInfo.colorAttachments[0].renderTarget = colorTarget;
            passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_CLEAR;
            passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
            passInfo.colorAttachments[0].clearColor[0] = 0.08f;
            passInfo.colorAttachments[0].clearColor[1] = 0.085f;
            passInfo.colorAttachments[0].clearColor[2] = 0.1f;
            passInfo.colorAttachments[0].clearColor[3] = 1.0f;
            passInfo.hasDepthAttachment = 1;
            passInfo.depthAttachment.renderTarget = surface.depthTarget;
            passInfo.depthAttachment.loadOp = AGFX_LOAD_OPERATION_CLEAR;
            passInfo.depthAttachment.storeOp = AGFX_STORE_OPERATION_STORE;
            passInfo.depthAttachment.clearDepth = settings.reverseZ ? 0.0f : 1.0f;
            agfxRenderPassEnd(agfxRenderPassBegin(cmd, &passInfo));
        }

        // Debug draw
        glm::mat4 projection = camera.Projection(aspect, settings.reverseZ);

        aimdExecuteInfo executeInfo = {};
        executeInfo.commandBuffer = cmd;
        executeInfo.colorTarget = colorTarget;
        executeInfo.colorFormat = surface.colorFormat;
        executeInfo.depthTarget = settings.useDepth ? surface.depthTarget : nullptr;
        executeInfo.depthFormat = settings.useDepth ? AGFX_TEXTURE_FORMAT_DEPTH32F : AGFX_TEXTURE_FORMAT_UNKNOWN;
        executeInfo.width = surface.width;
        executeInfo.height = surface.height;
        memcpy(executeInfo.view, glm::value_ptr(view), sizeof(executeInfo.view));
        memcpy(executeInfo.projection, glm::value_ptr(projection), sizeof(executeInfo.projection));
        executeInfo.reverseZ = settings.reverseZ;
        executeInfo.depthWrite = settings.depthWrite;
        executeInfo.frameIndex = frameIndex;
        executeInfo.textCallback = DrawTextWithImGui;
        aimdExecute(&executeInfo);

        // UI (labels were added to the background draw list by the text callback)
        ImGui::Render();
        {
            agfxRenderPassCreateInfo passInfo = {};
            passInfo.name = "ImGui";
            passInfo.width = surface.width;
            passInfo.height = surface.height;
            passInfo.colorAttachmentCount = 1;
            passInfo.colorAttachments[0].renderTarget = colorTarget;
            passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_LOAD;
            passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
            agfxRenderPass* pass = agfxRenderPassBegin(cmd, &passInfo);
            ImGui_ImplAGFX_RenderDrawData(ImGui::GetDrawData(), pass, surface.width, surface.height, frameIndex);
            agfxRenderPassEnd(pass);
        }

        agfxCommandBufferTextureBarrier(cmd, backBuffer, AGFX_RESOURCE_STATE_RENDER_TARGET, AGFX_RESOURCE_STATE_PRESENT,
                                        AGFX_SUBRESOURCE_ALL_MIPS, AGFX_SUBRESOURCE_ALL_LAYERS, 0);
        agfxCommandBufferEnd(cmd);
        agfxCommandQueueSubmit(queue, &cmd, 1);
        frameFenceValues[frameIndex] = ++fenceValue;
        agfxCommandQueueSignal(queue, fence, fenceValue);
        agfxSwapChainPresent(surface.swapChain);
        frameCounter++;
    }

    agfxDeviceWaitIdle(device);

    ImGui_ImplAGFX_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    aimdContextDestroy(aimd);
    agfxComputePipelineDestroy(device, scenePipeline);
    agfxShaderModuleDestroy(device, sceneShader);

    for (agfxRenderTarget* target : frameColorTargets)
        if (target)
            agfxRenderTargetDestroy(device, target);
    DestroySurface(device, surface);
    agfxFenceDestroy(device, fence);
    for (agfxCommandBuffer* commandBuffer : commandBuffers)
        agfxCommandBufferDestroy(device, commandBuffer);
    agfxCommandQueueDestroy(device, queue);
    agfxDeviceDestroy(device);

#if defined(__APPLE__)
    SDL_Metal_DestroyView(metalView);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
