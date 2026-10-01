#include "Emerald/Renderer/Renderer.h"

#include <cctype>
#include <cstring>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

// Picks the swapchain present mode. VSYNC is the only mode every backend must support.
SDL_GPUPresentMode ChoosePresentMode(SDL_GPUDevice* device, SDL_Window* window, bool vsync)
{
    if (vsync)
        return SDL_GPU_PRESENTMODE_VSYNC;
    // Without vsync prefer MAILBOX (no tearing), then IMMEDIATE (may tear).
    if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX))
        return SDL_GPU_PRESENTMODE_MAILBOX;
    if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
        return SDL_GPU_PRESENTMODE_IMMEDIATE;
    return SDL_GPU_PRESENTMODE_VSYNC;
}

const char* PresentModeName(SDL_GPUPresentMode mode)
{
    switch (mode) {
    case SDL_GPU_PRESENTMODE_VSYNC:
        return "vsync";
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return "mailbox";
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return "immediate";
    }
    return "unknown";
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (usize i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Forces SDL's driver choice: a name, or nullptr for auto. Override priority, so it also wins
// over the SDL_GPU_DRIVER environment variable.
void ForceGpuDriverHint(const char* name)
{
    SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, name, SDL_HINT_OVERRIDE);
}

} // namespace

std::optional<std::string_view> NormalizeGpuDriver(std::string_view value)
{
    if (EqualsIgnoreCase(value, "auto"))
        return std::string_view();
    if (EqualsIgnoreCase(value, "vulkan"))
        return "vulkan";
    if (EqualsIgnoreCase(value, "d3d12") || EqualsIgnoreCase(value, "direct3d12"))
        return "direct3d12";
    if (EqualsIgnoreCase(value, "metal"))
        return "metal";
    return std::nullopt;
}

std::optional<std::string_view> FindGpuArg(std::span<char* const> args)
{
    constexpr std::string_view kFlag = "--gpu";
    std::optional<std::string_view> value;
    for (usize i = 1; i < args.size(); ++i) {
        if (!args[i])
            continue;
        const std::string_view arg(args[i]);
        if (arg == kFlag && i + 1 < args.size() && args[i + 1])
            value = args[++i];
        else if (arg.starts_with(kFlag) && arg.size() > kFlag.size() && arg[kFlag.size()] == '=')
            value = arg.substr(kFlag.size() + 1);
    }
    return value;
}

Renderer::~Renderer()
{
    Shutdown();
}

bool Renderer::Init(SDL_Window* window, bool vsync, SDL_GPUShaderFormat shaderFormats,
                    std::optional<std::string_view> driver)
{
    // SDL only picks a backend that can consume one of the shader formats we ship:
    // SPIR-V -> Vulkan, DXIL -> Direct3D 12, MSL -> Metal.
#ifdef NDEBUG
    constexpr bool debugMode = false;
#else
    constexpr bool debugMode = true; // enables validation layers / extra checks where available
#endif

    // SDL reads the driver from the SDL_GPU_DRIVER hint (or environment variable).
    if (driver)
        ForceGpuDriverHint(driver->empty() ? nullptr : std::string(*driver).c_str());
    m_Device = SDL_CreateGPUDevice(shaderFormats, debugMode, nullptr);
    if (!m_Device) {
        const char* requested = SDL_GetHint(SDL_HINT_GPU_DRIVER);
        if (!requested || !*requested)
            return false; // auto already failed
        // A specific driver was asked for (--gpu or SDL_GPU_DRIVER): try the others so the app
        // still starts.
        EM_CORE_ERROR("GPU driver '{}' failed ({}); retrying with auto", requested, SDL_GetError());
        ForceGpuDriverHint(nullptr);
        m_Device = SDL_CreateGPUDevice(shaderFormats, debugMode, nullptr);
        if (!m_Device)
            return false;
    }

    // Claiming the window creates its swapchain (SDR, vsync by default).
    if (!SDL_ClaimWindowForGPUDevice(m_Device, window)) {
        SDL_DestroyGPUDevice(m_Device);
        m_Device = nullptr;
        return false;
    }
    m_Window = window;
    m_VSync = vsync;

    const SDL_GPUPresentMode presentMode = ChoosePresentMode(m_Device, window, vsync);
    if (!SDL_SetGPUSwapchainParameters(m_Device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       presentMode))
        EM_CORE_WARN("SDL_SetGPUSwapchainParameters failed: {}", SDL_GetError());

    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(m_Device);
    const char* deviceName = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "?");
    EM_CORE_INFO("GPU device created: backend '{}', device '{}', present mode {}{}",
                 SDL_GetGPUDeviceDriver(m_Device), deviceName, PresentModeName(presentMode),
                 debugMode ? " (debug mode)" : "");
    return true;
}

void Renderer::Shutdown()
{
    if (!m_Device)
        return;
    // Resources may still be in use by in-flight frames; wait before destroying anything.
    SDL_WaitForGPUIdle(m_Device);
    if (m_CaptureTexture)
        SDL_ReleaseGPUTexture(m_Device, m_CaptureTexture);
    if (m_CaptureDownload)
        SDL_ReleaseGPUTransferBuffer(m_Device, m_CaptureDownload);
    m_CaptureTexture = nullptr;
    m_CaptureDownload = nullptr;
    if (m_Window)
        SDL_ReleaseWindowFromGPUDevice(m_Device, m_Window);
    SDL_DestroyGPUDevice(m_Device);
    m_Device = nullptr;
    m_Window = nullptr;
}

bool Renderer::SetVSync(bool vsync)
{
    if (!m_Device)
        return false;
    const SDL_GPUPresentMode mode = ChoosePresentMode(m_Device, m_Window, vsync);
    if (!SDL_SetGPUSwapchainParameters(m_Device, m_Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       mode)) {
        EM_CORE_WARN("SDL_SetGPUSwapchainParameters failed: {}", SDL_GetError());
        return false;
    }
    m_VSync = vsync;
    EM_CORE_INFO("Present mode {}", PresentModeName(mode));
    return true;
}

std::string_view Renderer::GetDriverName() const
{
    const char* name = m_Device ? SDL_GetGPUDeviceDriver(m_Device) : nullptr;
    return name ? std::string_view(name) : std::string_view();
}

SDL_GPUTextureFormat Renderer::GetSwapchainFormat() const
{
    return SDL_GetGPUSwapchainTextureFormat(m_Device, m_Window);
}

bool Renderer::BeginFrame()
{
    m_CommandBuffer = SDL_AcquireGPUCommandBuffer(m_Device);
    if (!m_CommandBuffer) {
        EM_CORE_ERROR("SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return false;
    }

    // Blocks until a swapchain image is free (this is where vsync throttles the loop).
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_CommandBuffer, m_Window, &m_SwapchainTexture,
                                               &m_FrameWidth, &m_FrameHeight)) {
        EM_CORE_ERROR("SDL_WaitAndAcquireGPUSwapchainTexture failed: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(m_CommandBuffer);
        m_CommandBuffer = nullptr;
        return false;
    }

    // A null texture is not an error: the window is minimized/occluded. Submit the (empty)
    // command buffer anyway - every acquired command buffer must be submitted or cancelled.
    if (!m_SwapchainTexture) {
        SDL_SubmitGPUCommandBuffer(m_CommandBuffer);
        m_CommandBuffer = nullptr;
        return false;
    }

    m_RenderTarget = m_SwapchainTexture;
    if (m_ScreenshotPath) {
        if (SDL_GPUTexture* capture = GetCaptureTarget(m_FrameWidth, m_FrameHeight))
            m_RenderTarget = capture;
        else
            m_ScreenshotPath.reset();
    }
    return true;
}

SDL_GPURenderPass* Renderer::BeginRenderPass(const SDL_FColor& clearColor)
{
    return BeginRenderPass(m_RenderTarget, clearColor);
}

SDL_GPURenderPass* Renderer::BeginRenderPass(SDL_GPUTexture* texture, const SDL_FColor& clearColor,
                                             bool clear)
{
    SDL_GPUColorTargetInfo target{};
    target.texture = texture;
    target.clear_color = clearColor;
    // Start from the clear color (or the texture's contents)...
    target.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE; // ...and keep what we draw
    m_RenderPass = SDL_BeginGPURenderPass(m_CommandBuffer, &target, 1, nullptr);
    return m_RenderPass;
}

void Renderer::EndRenderPass()
{
    SDL_EndGPURenderPass(m_RenderPass);
    m_RenderPass = nullptr;
}

void Renderer::EndFrame()
{
    if (m_RenderTarget != m_SwapchainTexture) {
        // Screenshot frame: copy the offscreen image to the CPU and to the swapchain.
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(m_CommandBuffer);
        SDL_GPUTextureRegion region{};
        region.texture = m_CaptureTexture;
        region.w = m_FrameWidth;
        region.h = m_FrameHeight;
        region.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = m_CaptureDownload;
        SDL_DownloadFromGPUTexture(copy, &region, &destination);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUBlitInfo blit{};
        blit.source.texture = m_CaptureTexture;
        blit.source.w = m_FrameWidth;
        blit.source.h = m_FrameHeight;
        blit.destination.texture = m_SwapchainTexture;
        blit.destination.w = m_FrameWidth;
        blit.destination.h = m_FrameHeight;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(m_CommandBuffer, &blit);

        // A fence lets the CPU wait until the GPU has actually finished the download.
        m_CaptureFence = SDL_SubmitGPUCommandBufferAndAcquireFence(m_CommandBuffer);
        FinishScreenshot();
    } else if (!SDL_SubmitGPUCommandBuffer(m_CommandBuffer)) {
        EM_CORE_ERROR("SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
    }

    m_CommandBuffer = nullptr;
    m_SwapchainTexture = nullptr;
    m_RenderTarget = nullptr;
}

SDL_GPUBuffer* Renderer::CreateBuffer(SDL_GPUBufferUsageFlags usage, const void* data, u32 size)
{
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = usage;
    bufferInfo.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(m_Device, &bufferInfo);
    if (!buffer) {
        EM_CORE_ERROR("SDL_CreateGPUBuffer failed: {}", SDL_GetError());
        return nullptr;
    }

    // GPU buffers are not CPU-visible. Data goes through a mappable "transfer buffer" first...
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
    if (!transfer) {
        EM_CORE_ERROR("SDL_CreateGPUTransferBuffer failed: {}", SDL_GetError());
        SDL_ReleaseGPUBuffer(m_Device, buffer);
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(m_Device, transfer, false);
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(m_Device, transfer);

    // ...and is then copied into the real buffer by the GPU during a copy pass.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTransferBufferLocation source{transfer, 0};
    const SDL_GPUBufferRegion destination{buffer, 0, size};
    SDL_UploadToGPUBuffer(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    // Safe right away: SDL defers the actual release until the GPU no longer uses it.
    SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
    return buffer;
}

SDL_GPUTexture* Renderer::GetCaptureTarget(u32 width, u32 height)
{
    if (m_CaptureTexture && m_CaptureWidth == width && m_CaptureHeight == height)
        return m_CaptureTexture;

    if (m_CaptureTexture)
        SDL_ReleaseGPUTexture(m_Device, m_CaptureTexture);
    if (m_CaptureDownload)
        SDL_ReleaseGPUTransferBuffer(m_Device, m_CaptureDownload);

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = GetSwapchainFormat(); // same format, so pipelines stay compatible
    // COLOR_TARGET to render into it, SAMPLER so it can be a blit source.
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    m_CaptureTexture = SDL_CreateGPUTexture(m_Device, &textureInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = width * height * 4;
    m_CaptureDownload = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);

    if (!m_CaptureTexture || !m_CaptureDownload) {
        EM_CORE_ERROR("Could not create screenshot resources: {}", SDL_GetError());
        return nullptr;
    }
    m_CaptureWidth = width;
    m_CaptureHeight = height;
    return m_CaptureTexture;
}

void Renderer::FinishScreenshot()
{
    const std::filesystem::path path = *std::exchange(m_ScreenshotPath, std::nullopt);
    if (!m_CaptureFence) {
        EM_CORE_ERROR("Screenshot submit failed: {}", SDL_GetError());
        return;
    }
    SDL_WaitForGPUFences(m_Device, true, &m_CaptureFence, 1);
    SDL_ReleaseGPUFence(m_Device, m_CaptureFence);
    m_CaptureFence = nullptr;

    const SDL_GPUTextureFormat format = GetSwapchainFormat();
    const bool bgra = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                      format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
                      format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    if (!bgra && !rgba) {
        EM_CORE_ERROR("Screenshot: unsupported swapchain format {}", static_cast<i32>(format));
        return;
    }

    Image image;
    image.Width = static_cast<i32>(m_CaptureWidth);
    image.Height = static_cast<i32>(m_CaptureHeight);
    image.Pixels.resize(static_cast<usize>(m_CaptureWidth) * m_CaptureHeight * 4);
    const auto* src =
        static_cast<const u8*>(SDL_MapGPUTransferBuffer(m_Device, m_CaptureDownload, false));
    for (usize i = 0; i < image.Pixels.size(); i += 4) {
        image.Pixels[i + 0] = src[i + (bgra ? 2 : 0)];
        image.Pixels[i + 1] = src[i + 1];
        image.Pixels[i + 2] = src[i + (bgra ? 0 : 2)];
        image.Pixels[i + 3] = 255;
    }
    SDL_UnmapGPUTransferBuffer(m_Device, m_CaptureDownload);

    if (image.SavePNG(path))
        EM_CORE_INFO("Screenshot saved to {}", path.string());
    else
        EM_CORE_ERROR("Failed to write screenshot {}", path.string());
}

} // namespace Emerald
