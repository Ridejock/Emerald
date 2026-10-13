#include "Emerald/Renderer/Texture.h"

#include <atomic>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Renderer/Sprite.h"

namespace Emerald {

namespace {

u32 NextTextureId()
{
    static std::atomic<u32> next{1};
    return next++;
}

SDL_GPUSampler* CreateSampler(SDL_GPUDevice* device, const TextureOptions& options)
{
    const SDL_GPUFilter filter =
        options.Filter == TextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    const SDL_GPUSamplerAddressMode address = options.Wrap == TextureWrap::Clamp
                                                  ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                  : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = filter;
    info.mag_filter = filter;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST; // no mipmaps (one level)
    info.address_mode_u = address;
    info.address_mode_v = address;
    info.address_mode_w = address;
    return SDL_CreateGPUSampler(device, &info);
}

} // namespace

Sprite Sprite::FromTexture(const Texture& texture)
{
    return {&texture, {{0.0f, 0.0f}, texture.GetSize()}};
}

Texture::~Texture()
{
    Release();
}

Texture::Texture(Texture&& other) noexcept
    : m_Device(std::exchange(other.m_Device, nullptr)),
      m_Texture(std::exchange(other.m_Texture, nullptr)),
      m_Sampler(std::exchange(other.m_Sampler, nullptr)), m_Width(std::exchange(other.m_Width, 0)),
      m_Height(std::exchange(other.m_Height, 0)), m_Id(std::exchange(other.m_Id, 0))
{
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other) {
        Release();
        m_Device = std::exchange(other.m_Device, nullptr);
        m_Texture = std::exchange(other.m_Texture, nullptr);
        m_Sampler = std::exchange(other.m_Sampler, nullptr);
        m_Width = std::exchange(other.m_Width, 0);
        m_Height = std::exchange(other.m_Height, 0);
        m_Id = std::exchange(other.m_Id, 0);
    }
    return *this;
}

void Texture::Release()
{
    // Safe while the GPU still uses them: SDL releases them once in-flight work is done.
    if (m_Device) {
        if (m_Sampler)
            SDL_ReleaseGPUSampler(m_Device, m_Sampler);
        if (m_Texture)
            SDL_ReleaseGPUTexture(m_Device, m_Texture);
    }
    m_Device = nullptr;
    m_Texture = nullptr;
    m_Sampler = nullptr;
}

Texture Texture::CreateWithoutGpu(u32 width, u32 height)
{
    Texture texture;
    texture.m_Width = width;
    texture.m_Height = height;
    texture.m_Id = NextTextureId();
    return texture;
}

std::optional<Texture> Texture::Create(SDL_GPUDevice* device, const Image& image,
                                       const TextureOptions& options)
{
    // SDL GPU only guarantees 16384 px on a side on every backend; the limit also keeps the
    // byte count below in u32 range.
    constexpr i32 kMaxSide = 16384;
    if (!device || image.Width <= 0 || image.Height <= 0 || image.Width > kMaxSide ||
        image.Height > kMaxSide ||
        image.Pixels.size() != static_cast<usize>(image.Width) * image.Height * 4) {
        EM_CORE_ERROR("Texture::Create: invalid image or device ({} x {})", image.Width,
                      image.Height);
        return std::nullopt;
    }
    const u32 width = static_cast<u32>(image.Width);
    const u32 height = static_cast<u32>(image.Height);
    const u32 bytes = width * height * 4;

    Texture texture = CreateWithoutGpu(width, height);
    texture.m_Device = device;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; // byte order of Image::Pixels
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    texture.m_Texture = SDL_CreateGPUTexture(device, &info);
    texture.m_Sampler = CreateSampler(device, options);
    if (!texture.m_Texture || !texture.m_Sampler) {
        EM_CORE_ERROR("Texture::Create: {}", SDL_GetError());
        return std::nullopt; // `texture` releases whatever was created
    }

    // Same route as buffers: CPU -> transfer buffer -> copy pass -> GPU texture.
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    void* mapped = transfer ? SDL_MapGPUTransferBuffer(device, transfer, false) : nullptr;
    if (!mapped) {
        EM_CORE_ERROR("Texture::Create: could not create the upload buffer: {}", SDL_GetError());
        if (transfer)
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        return std::nullopt;
    }
    std::memcpy(mapped, image.Pixels.data(), bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        EM_CORE_ERROR("Texture::Create: SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return std::nullopt;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = width; // tightly packed rows
    source.rows_per_layer = height;
    SDL_GPUTextureRegion destination{};
    destination.texture = texture.m_Texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer); // released once the upload is done

    return texture;
}

std::optional<Texture> Texture::Load(SDL_GPUDevice* device, const std::filesystem::path& path,
                                     const TextureOptions& options)
{
    const std::optional<Image> image = Image::LoadFromFile(path); // logs failures
    if (!image)
        return std::nullopt;
    std::optional<Texture> texture = Create(device, *image, options);
    if (texture)
        EM_CORE_INFO("Loaded texture {} ({}x{})", path.string(), texture->GetWidth(),
                     texture->GetHeight());
    return texture;
}

} // namespace Emerald
