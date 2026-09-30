#include "Emerald/Renderer/Renderer2D.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Renderer/Pipeline.h"
#include "Emerald/Renderer/Shader.h"
#include "Emerald/Renderer/Texture.h"

namespace Emerald {

Vec2 Transform2D::Apply(const Vec2& point) const
{
    const Vec2 p = point * Scale;
    const f32 c = std::cos(Rotation);
    const f32 s = std::sin(Rotation);
    return Position + Vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

Renderer2D::~Renderer2D()
{
    Shutdown();
}

bool Renderer2D::Init(SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat)
{
    m_Device = device;

    // Resource counts (1 vertex uniform buffer) come from the .json reflection files.
    SDL_GPUShader* vertex = LoadShader(device, {.Name = "Renderer2D.vert"});
    SDL_GPUShader* fragment = LoadShader(device, {.Name = "Renderer2D.frag"});

    const SDL_GPUVertexBufferDescription buffers[] = {{
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    }};
    const SDL_GPUVertexAttribute attributes[] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(Vertex, Position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
         .offset = offsetof(Vertex, Color)},
    };

    if (vertex && fragment) {
        GraphicsPipelineDesc desc{.VertexShader = vertex,
                                  .FragmentShader = fragment,
                                  .VertexBuffers = buffers,
                                  .VertexAttributes = attributes,
                                  .ColorFormat = colorFormat,
                                  .Primitive = SDL_GPU_PRIMITIVETYPE_LINELIST,
                                  .AlphaBlend = true};
        m_Pipeline = CreateGraphicsPipeline(device, desc);
        desc.AdditiveBlend = true; // same shaders, different blend state
        m_AdditivePipeline = CreateGraphicsPipeline(device, desc);
    }
    if (vertex)
        SDL_ReleaseGPUShader(device, vertex);
    if (fragment)
        SDL_ReleaseGPUShader(device, fragment);

    if (!m_Pipeline || !m_AdditivePipeline) {
        EM_CORE_ERROR("Renderer2D: could not create its pipeline; 2D shapes will not be drawn. "
                      "Did the app's CMake call emerald_add_shaders(<target>)?");
        return false;
    }

    // Sprites: textured triangles. The fragment shader samples one texture (reflection: 1
    // sampler).
    SDL_GPUShader* spriteVertex = LoadShader(device, {.Name = "Sprite.vert"});
    SDL_GPUShader* spriteFragment = LoadShader(device, {.Name = "Sprite.frag"});
    const SDL_GPUVertexBufferDescription spriteBuffers[] = {{
        .slot = 0,
        .pitch = sizeof(SpriteVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    }};
    const SDL_GPUVertexAttribute spriteAttributes[] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(SpriteVertex, Position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(SpriteVertex, TexCoord)},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
         .offset = offsetof(SpriteVertex, Color)},
    };
    if (spriteVertex && spriteFragment) {
        GraphicsPipelineDesc desc{.VertexShader = spriteVertex,
                                  .FragmentShader = spriteFragment,
                                  .VertexBuffers = spriteBuffers,
                                  .VertexAttributes = spriteAttributes,
                                  .ColorFormat = colorFormat,
                                  .Primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                  .AlphaBlend = true};
        m_SpritePipeline = CreateGraphicsPipeline(device, desc);
        desc.AdditiveBlend = true;
        m_AdditiveSpritePipeline = CreateGraphicsPipeline(device, desc);
    }
    if (spriteVertex)
        SDL_ReleaseGPUShader(device, spriteVertex);
    if (spriteFragment)
        SDL_ReleaseGPUShader(device, spriteFragment);
    if (!m_SpritePipeline || !m_AdditiveSpritePipeline) {
        EM_CORE_ERROR("Renderer2D: could not create the sprite pipeline; sprites will not be "
                      "drawn (lines still work)");
        return false;
    }
    return true;
}

void Renderer2D::Shutdown()
{
    if (!m_Device)
        return;
    // The caller (Application) waits for the GPU to be idle before shutting renderers down.
    for (SDL_GPUGraphicsPipeline* pipeline :
         {m_Pipeline, m_SpritePipeline, m_AdditivePipeline, m_AdditiveSpritePipeline})
        if (pipeline)
            SDL_ReleaseGPUGraphicsPipeline(m_Device, pipeline);
    m_LineStream.Release(m_Device);
    m_SpriteStream.Release(m_Device);
    m_Pipeline = nullptr;
    m_SpritePipeline = nullptr;
    m_AdditivePipeline = nullptr;
    m_AdditiveSpritePipeline = nullptr;
    m_Device = nullptr;
}

void Renderer2D::Begin(const Mat4& viewProjection, const SDL_Rect& clip)
{
    assert(!m_InBatch && "Renderer2D::Begin called twice without End");
    Batch batch;
    batch.ViewProjection = viewProjection;
    batch.Clip = clip;
    batch.FirstVertex = static_cast<u32>(m_Vertices.size());
    batch.FirstCommand = static_cast<u32>(m_Commands.size());
    m_Batches.push_back(batch);
    m_InBatch = true;
    m_BlendMode = BlendMode::Alpha;
}

void Renderer2D::End()
{
    if (!m_InBatch)
        return;
    CloseCommand();
    Batch& batch = m_Batches.back();
    batch.VertexCount = static_cast<u32>(m_Vertices.size()) - batch.FirstVertex;
    m_InBatch = false;
}

void Renderer2D::UseCommand(CommandType type, const Texture* texture)
{
    Batch& batch = m_Batches.back();
    if (batch.CommandCount > 0) {
        const DrawCommand& last = m_Commands.back();
        const bool sameTexture = type == CommandType::Lines || last.TextureId == texture->GetId();
        if (last.Type == type && sameTexture && last.Blend == m_BlendMode)
            return; // keep adding to the current run
        CloseCommand();
    }
    DrawCommand command;
    command.Type = type;
    command.Blend = m_BlendMode;
    if (type == CommandType::Lines) {
        command.FirstVertex = static_cast<u32>(m_Vertices.size());
    } else {
        command.FirstVertex = static_cast<u32>(m_SpriteVertices.size());
        command.TextureId = texture->GetId();
        command.GpuTexture = texture->GetGpuTexture();
        command.Sampler = texture->GetSampler();
    }
    m_Commands.push_back(command);
    ++batch.CommandCount;
}

void Renderer2D::CloseCommand()
{
    if (m_Batches.empty() || m_Batches.back().CommandCount == 0)
        return;
    DrawCommand& last = m_Commands.back();
    const usize end = last.Type == CommandType::Lines ? m_Vertices.size() : m_SpriteVertices.size();
    last.VertexCount = static_cast<u32>(end) - last.FirstVertex;
}

bool Renderer2D::CanDraw() const
{
    assert(m_InBatch && "Renderer2D: call Begin() before drawing");
    return m_InBatch;
}

void Renderer2D::DrawLine(const Vec2& a, const Vec2& b, const Vec4& color)
{
    if (!CanDraw())
        return;
    UseCommand(CommandType::Lines);
    const u32 packed = PackColor(color);
    m_Vertices.push_back({a, packed});
    m_Vertices.push_back({b, packed});
}

void Renderer2D::DrawPolyline(std::span<const Vec2> points, const Vec4& color,
                              const Transform2D& transform, bool closed)
{
    if (!CanDraw() || points.size() < 2)
        return;
    UseCommand(CommandType::Lines);
    const u32 packed = PackColor(color);
    // A line list needs both end points of every segment, so inner points appear twice.
    Vec2 previous = transform.Apply(points[0]);
    const Vec2 first = previous;
    for (usize i = 1; i < points.size(); ++i) {
        const Vec2 current = transform.Apply(points[i]);
        m_Vertices.push_back({previous, packed});
        m_Vertices.push_back({current, packed});
        previous = current;
    }
    if (closed && points.size() > 2) {
        m_Vertices.push_back({previous, packed});
        m_Vertices.push_back({first, packed});
    }
}

void Renderer2D::DrawPolygon(std::span<const Vec2> points, const Vec4& color,
                             const Transform2D& transform)
{
    DrawPolyline(points, color, transform, true);
}

void Renderer2D::DrawCircle(const Vec2& center, f32 radius, const Vec4& color, u32 segments)
{
    if (!CanDraw() || segments < 3)
        return;
    UseCommand(CommandType::Lines);
    const u32 packed = PackColor(color);
    Vec2 previous = center + Vec2(radius, 0.0f);
    for (u32 i = 1; i <= segments; ++i) {
        const f32 angle = TwoPi * static_cast<f32>(i) / static_cast<f32>(segments);
        const Vec2 current = center + Vec2(std::cos(angle), std::sin(angle)) * radius;
        m_Vertices.push_back({previous, packed});
        m_Vertices.push_back({current, packed});
        previous = current;
    }
}

void Renderer2D::DrawRect(const Vec2& topLeft, const Vec2& size, const Vec4& color)
{
    const Vec2 corners[] = {topLeft, topLeft + Vec2(size.x, 0.0f), topLeft + size,
                            topLeft + Vec2(0.0f, size.y)};
    DrawPolygon(corners, color);
}

void Renderer2D::DrawSprite(const Texture& texture, const Vec2& position,
                            const SpriteOptions& options)
{
    DrawSprite(Sprite::FromTexture(texture), position, options);
}

void Renderer2D::DrawSprite(const Sprite& sprite, const Vec2& position,
                            const SpriteOptions& options)
{
    if (!CanDraw() || !sprite.Source || sprite.Source->GetWidth() == 0 ||
        sprite.Source->GetHeight() == 0)
        return;
    UseCommand(CommandType::Sprites, sprite.Source);

    const Vec2 size = options.Size.x != 0.0f || options.Size.y != 0.0f
                          ? options.Size
                          : sprite.Region.Size * options.Scale;
    // The origin's offset from the top-left corner (before rotation).
    const Vec2 pivot = options.Origin * size;
    Vec2 at = position;
    if (options.PixelSnap) {
        const Vec2 topLeft = position - pivot;
        at = Vec2(std::round(topLeft.x), std::round(topLeft.y)) + pivot;
    }

    // Texture coordinates: the region in 0..1 units; flipping swaps the edges.
    const Vec2 textureSize = sprite.Source->GetSize();
    Vec2 uv0 = sprite.Region.Position / textureSize;
    Vec2 uv1 = (sprite.Region.Position + sprite.Region.Size) / textureSize;
    if (options.FlipX)
        std::swap(uv0.x, uv1.x);
    if (options.FlipY)
        std::swap(uv0.y, uv1.y);

    // Corners around the origin, rotated, then moved into place (like Transform2D).
    const f32 c = std::cos(options.Rotation);
    const f32 s = std::sin(options.Rotation);
    const auto corner = [&](f32 fx, f32 fy) {
        const Vec2 local = Vec2(fx * size.x, fy * size.y) - pivot;
        return at + Vec2(local.x * c - local.y * s, local.x * s + local.y * c);
    };
    const u32 color = PackColor(options.Tint);
    const SpriteVertex topLeft{corner(0.0f, 0.0f), {uv0.x, uv0.y}, color};
    const SpriteVertex topRight{corner(1.0f, 0.0f), {uv1.x, uv0.y}, color};
    const SpriteVertex bottomRight{corner(1.0f, 1.0f), {uv1.x, uv1.y}, color};
    const SpriteVertex bottomLeft{corner(0.0f, 1.0f), {uv0.x, uv1.y}, color};
    // Two triangles; no index buffer, so shared corners are repeated.
    m_SpriteVertices.insert(m_SpriteVertices.end(),
                            {topLeft, topRight, bottomRight, topLeft, bottomRight, bottomLeft});
}

void Renderer2D::DrawString(const Font& font, std::string_view text, const Vec2& position,
                            const Vec4& color, f32 scale, TextAlign align)
{
    if (!CanDraw() || text.empty())
        return;
    m_TextGlyphs.clear();
    font.LayoutText(text, align, m_TextGlyphs);
    for (const PlacedGlyph& placed : m_TextGlyphs) {
        const Sprite sprite{&font.GetTexture(), placed.Source->Region};
        DrawSprite(sprite, position + placed.Position * scale,
                   {.Size = placed.Source->Size * scale,
                    .Origin = {0.0f, 0.0f},
                    .Tint = color,
                    .PixelSnap = font.IsPixelFont()});
    }
}

u32 Renderer2D::PackColor(const Vec4& color)
{
    const auto channel = [](f32 v) {
        return static_cast<u32>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return channel(color.x) | channel(color.y) << 8 | channel(color.z) << 16 |
           channel(color.w) << 24;
}

bool Renderer2D::GpuStream::Ensure(SDL_GPUDevice* device, u32 bytes)
{
    if (bytes <= Capacity)
        return true;

    // Grow to the next power of two (at least 64 KiB) so resizes are rare.
    u32 capacity = Max(Capacity, 64u * 1024u);
    while (capacity < bytes)
        capacity *= 2;

    // Safe even if the last frame still uses them: SDL releases them once the GPU is done.
    Release(device);

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = capacity;
    Buffer = SDL_CreateGPUBuffer(device, &bufferInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = capacity;
    Transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

    if (!Buffer || !Transfer) {
        EM_CORE_ERROR("Renderer2D: could not allocate {} byte buffers: {}", capacity,
                      SDL_GetError());
        return false;
    }
    Capacity = capacity;
    EM_CORE_TRACE("Renderer2D: {} vertex buffer grown to {} KiB", Name, capacity / 1024);
    return true;
}

bool Renderer2D::GpuStream::Fill(SDL_GPUDevice* device, const void* data, u32 bytes)
{
    if (!Ensure(device, bytes))
        return false;
    // cycle = true: if the GPU is still reading last frame's data, SDL hands us a fresh
    // internal buffer instead of making us wait (or overwriting data in use).
    void* mapped = SDL_MapGPUTransferBuffer(device, Transfer, true);
    if (!mapped) {
        EM_CORE_ERROR("Renderer2D: SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
        return false;
    }
    std::memcpy(mapped, data, bytes);
    SDL_UnmapGPUTransferBuffer(device, Transfer);
    return true;
}

void Renderer2D::GpuStream::Release(SDL_GPUDevice* device)
{
    if (Buffer)
        SDL_ReleaseGPUBuffer(device, Buffer);
    if (Transfer)
        SDL_ReleaseGPUTransferBuffer(device, Transfer);
    Buffer = nullptr;
    Transfer = nullptr;
    Capacity = 0;
}

void Renderer2D::Upload(SDL_GPUCommandBuffer* commandBuffer)
{
    End(); // tolerate a missing End()
    m_LinesUploaded = false;
    m_SpritesUploaded = false;
    if (!m_Device)
        return;

    const u32 lineBytes = static_cast<u32>(m_Vertices.size() * sizeof(Vertex));
    const u32 spriteBytes = static_cast<u32>(m_SpriteVertices.size() * sizeof(SpriteVertex));
    const bool lines =
        m_Pipeline && lineBytes > 0 && m_LineStream.Fill(m_Device, m_Vertices.data(), lineBytes);
    const bool sprites = m_SpritePipeline && spriteBytes > 0 &&
                         m_SpriteStream.Fill(m_Device, m_SpriteVertices.data(), spriteBytes);
    if (!lines && !sprites)
        return;

    // One copy pass for both.
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commandBuffer);
    if (lines) {
        const SDL_GPUTransferBufferLocation source{m_LineStream.Transfer, 0};
        const SDL_GPUBufferRegion destination{m_LineStream.Buffer, 0, lineBytes};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    }
    if (sprites) {
        const SDL_GPUTransferBufferLocation source{m_SpriteStream.Transfer, 0};
        const SDL_GPUBufferRegion destination{m_SpriteStream.Buffer, 0, spriteBytes};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    }
    SDL_EndGPUCopyPass(copy);
    m_LinesUploaded = lines;
    m_SpritesUploaded = sprites;
}

void Renderer2D::Render(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass,
                        u32 targetWidth, u32 targetHeight)
{
    m_LastFrameLines = m_LinesUploaded ? GetLineCount() : 0;
    m_LastFrameSprites = m_SpritesUploaded ? GetSpriteCount() : 0;
    m_LastFrameDrawCalls = 0;
    SDL_GPUGraphicsPipeline* bound = nullptr;

    for (const Batch& batch : m_Batches) {
        if (batch.CommandCount == 0)
            continue;
        // The scissor rectangle stays set for later draws, so set it for every batch: the
        // clip rectangle (kept inside the target, as Metal requires), or the whole target.
        const SDL_Rect full{0, 0, static_cast<i32>(targetWidth), static_cast<i32>(targetHeight)};
        SDL_Rect scissor = full;
        if (batch.Clip.w > 0 && batch.Clip.h > 0 &&
            !SDL_GetRectIntersection(&batch.Clip, &full, &scissor))
            continue; // clipped away entirely
        SDL_SetGPUScissor(renderPass, &scissor);

        for (u32 i = 0; i < batch.CommandCount; ++i) {
            const DrawCommand& command = m_Commands[batch.FirstCommand + i];
            const bool isLines = command.Type == CommandType::Lines;
            if (command.VertexCount == 0 || (isLines && !m_LinesUploaded) ||
                (!isLines && (!m_SpritesUploaded || !command.GpuTexture)))
                continue; // nothing to draw, or no GPU texture (e.g. CreateWithoutGpu)

            // Switch pipelines (and their vertex buffer) only when the kind or blend changes.
            const bool additive = command.Blend == BlendMode::Additive;
            SDL_GPUGraphicsPipeline* pipeline =
                isLines ? (additive ? m_AdditivePipeline : m_Pipeline)
                        : (additive ? m_AdditiveSpritePipeline : m_SpritePipeline);
            if (pipeline != bound) {
                SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
                const SDL_GPUBufferBinding binding{
                    isLines ? m_LineStream.Buffer : m_SpriteStream.Buffer, 0};
                SDL_BindGPUVertexBuffers(renderPass, 0, &binding, 1);
                bound = pipeline;
            }
            if (!isLines) {
                // Sampler slot 0 = register(t0/s0, space2) in Sprite.frag.hlsl.
                const SDL_GPUTextureSamplerBinding texture{command.GpuTexture, command.Sampler};
                SDL_BindGPUFragmentSamplers(renderPass, 0, &texture, 1);
            }
            // Uniform slot 0 = register(b0, space1) in both vertex shaders.
            SDL_PushGPUVertexUniformData(commandBuffer, 0, &batch.ViewProjection, sizeof(Mat4));
            SDL_DrawGPUPrimitives(renderPass, command.VertexCount, 1, command.FirstVertex, 0);
            ++m_LastFrameDrawCalls;
        }
    }
    Clear();
}

void Renderer2D::Clear()
{
    // clear() keeps the capacity, so steady-state frames do not allocate.
    m_Vertices.clear();
    m_SpriteVertices.clear();
    m_Commands.clear();
    m_Batches.clear();
    m_InBatch = false;
    m_LinesUploaded = false;
    m_SpritesUploaded = false;
}

} // namespace Emerald
