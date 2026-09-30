#pragma once

#include <filesystem>
#include <optional>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"

struct SDL_Window;

namespace Emerald {

// Thin wrapper around an SDL_GPUDevice that renders into one window's swapchain.
//
// A frame with SDL GPU always looks like this:
//   1. Acquire a command buffer          (BeginFrame)
//   2. Acquire the swapchain texture     (BeginFrame; may be null, e.g. window minimized)
//   3. Optional copy passes (uploads)    (between BeginFrame and BeginRenderPass)
//   4. One or more render passes         (BeginRenderPass / EndRenderPass)
//   5. Submit the command buffer         (EndFrame; this also presents the swapchain texture)
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Creates the GPU device (Vulkan, D3D12 or Metal - whatever SDL picks among the backends that
    // accept one of `shaderFormats`) and claims the window.
    // Returns false on failure; SDL_GetError() has the reason.
    bool Init(SDL_Window* window, bool vsync, SDL_GPUShaderFormat shaderFormats);
    void Shutdown();

    // Returns false if there is nothing to draw into this frame (e.g. the window is minimized).
    // In that case the frame is already finished; do not call the other frame functions.
    bool BeginFrame();
    // Starts a render pass that clears the swapchain texture to `clearColor`.
    SDL_GPURenderPass* BeginRenderPass(const SDL_FColor& clearColor);
    // Starts a render pass into any texture (e.g. an offscreen one for post-processing), cleared
    // to `clearColor` or, with `clear` = false, keeping what is already there.
    SDL_GPURenderPass* BeginRenderPass(SDL_GPUTexture* target, const SDL_FColor& clearColor,
                                       bool clear = true);
    void EndRenderPass();
    // Submits the command buffer; the swapchain texture is presented when the GPU is done.
    void EndFrame();

    // Creates a GPU buffer and uploads `data` into it: CPU -> transfer buffer -> copy pass -> GPU
    // buffer. The upload runs on its own command buffer, submitted before any later frame.
    [[nodiscard]] SDL_GPUBuffer* CreateBuffer(SDL_GPUBufferUsageFlags usage, const void* data,
                                              u32 size);

    // Saves the next rendered frame as a PNG (read back from the GPU after submission).
    void RequestScreenshot(std::filesystem::path path) { m_ScreenshotPath = std::move(path); }

    // Vsync on: frames wait for the display (no tearing, no wasted frames). Off: the fastest
    // mode the window supports (mailbox, else immediate). Takes effect from the next frame.
    bool SetVSync(bool vsync);
    [[nodiscard]] bool IsVSync() const { return m_VSync; }

    [[nodiscard]] SDL_GPUDevice* GetDevice() const { return m_Device; }
    [[nodiscard]] SDL_GPUCommandBuffer* GetCommandBuffer() const { return m_CommandBuffer; }
    [[nodiscard]] SDL_GPURenderPass* GetRenderPass() const { return m_RenderPass; }
    // This frame's output texture: the swapchain's, or the screenshot capture texture.
    [[nodiscard]] SDL_GPUTexture* GetRenderTarget() const { return m_RenderTarget; }
    [[nodiscard]] SDL_GPUTextureFormat GetSwapchainFormat() const;
    [[nodiscard]] u32 GetFrameWidth() const { return m_FrameWidth; }
    [[nodiscard]] u32 GetFrameHeight() const { return m_FrameHeight; }

private:
    SDL_GPUTexture* GetCaptureTarget(u32 width, u32 height);
    void FinishScreenshot();

    SDL_GPUDevice* m_Device = nullptr;
    SDL_Window* m_Window = nullptr;
    bool m_VSync = true;

    // Per-frame state (valid between BeginFrame and EndFrame).
    SDL_GPUCommandBuffer* m_CommandBuffer = nullptr;
    SDL_GPUTexture* m_SwapchainTexture = nullptr;
    SDL_GPUTexture* m_RenderTarget = nullptr; // swapchain, or the capture texture for screenshots
    SDL_GPURenderPass* m_RenderPass = nullptr;
    u32 m_FrameWidth = 0;
    u32 m_FrameHeight = 0;

    // Screenshot support: the frame is rendered into an offscreen texture (the swapchain texture
    // is write-only), which is then blitted to the swapchain and downloaded to the CPU.
    std::optional<std::filesystem::path> m_ScreenshotPath;
    SDL_GPUTexture* m_CaptureTexture = nullptr;
    u32 m_CaptureWidth = 0;
    u32 m_CaptureHeight = 0;
    SDL_GPUTransferBuffer* m_CaptureDownload = nullptr;
    SDL_GPUFence* m_CaptureFence = nullptr;
};

} // namespace Emerald
