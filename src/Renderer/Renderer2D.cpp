#include "Emerald/Renderer/Renderer2D.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Renderer/Pipeline.h"
#include "Emerald/Renderer/Shader.h"

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
        m_Pipeline = CreateGraphicsPipeline(device, {.VertexShader = vertex,
                                                     .FragmentShader = fragment,
                                                     .VertexBuffers = buffers,
                                                     .VertexAttributes = attributes,
                                                     .ColorFormat = colorFormat,
                                                     .Primitive = SDL_GPU_PRIMITIVETYPE_LINELIST,
                                                     .AlphaBlend = true});
    }
    if (vertex)
        SDL_ReleaseGPUShader(device, vertex);
    if (fragment)
        SDL_ReleaseGPUShader(device, fragment);

    if (!m_Pipeline) {
        EM_CORE_ERROR("Renderer2D: could not create its pipeline; 2D shapes will not be drawn. "
                      "Did the app's CMake call emerald_add_shaders(<target>)?");
        return false;
    }
    return true;
}

void Renderer2D::Shutdown()
{
    if (!m_Device)
        return;
    // The caller (Application) waits for the GPU to be idle before shutting renderers down.
    if (m_Pipeline)
        SDL_ReleaseGPUGraphicsPipeline(m_Device, m_Pipeline);
    if (m_VertexBuffer)
        SDL_ReleaseGPUBuffer(m_Device, m_VertexBuffer);
    if (m_TransferBuffer)
        SDL_ReleaseGPUTransferBuffer(m_Device, m_TransferBuffer);
    m_Pipeline = nullptr;
    m_VertexBuffer = nullptr;
    m_TransferBuffer = nullptr;
    m_Capacity = 0;
    m_Device = nullptr;
}

void Renderer2D::Begin(const Mat4& viewProjection)
{
    assert(!m_InBatch && "Renderer2D::Begin called twice without End");
    m_Batches.push_back({viewProjection, static_cast<u32>(m_Vertices.size()), 0});
    m_InBatch = true;
}

void Renderer2D::End()
{
    if (!m_InBatch)
        return;
    Batch& batch = m_Batches.back();
    batch.VertexCount = static_cast<u32>(m_Vertices.size()) - batch.FirstVertex;
    m_InBatch = false;
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
    const u32 packed = PackColor(color);
    m_Vertices.push_back({a, packed});
    m_Vertices.push_back({b, packed});
}

void Renderer2D::DrawPolyline(std::span<const Vec2> points, const Vec4& color,
                              const Transform2D& transform, bool closed)
{
    if (!CanDraw() || points.size() < 2)
        return;
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

u32 Renderer2D::PackColor(const Vec4& color)
{
    const auto channel = [](f32 v) {
        return static_cast<u32>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return channel(color.x) | channel(color.y) << 8 | channel(color.z) << 16 |
           channel(color.w) << 24;
}

bool Renderer2D::EnsureCapacity(u32 bytes)
{
    if (bytes <= m_Capacity)
        return true;

    // Grow to the next power of two (at least 64 KiB) so resizes are rare.
    u32 capacity = Max(m_Capacity, 64u * 1024u);
    while (capacity < bytes)
        capacity *= 2;

    // Safe even if the last frame still uses them: SDL releases them once the GPU is done.
    if (m_VertexBuffer)
        SDL_ReleaseGPUBuffer(m_Device, m_VertexBuffer);
    if (m_TransferBuffer)
        SDL_ReleaseGPUTransferBuffer(m_Device, m_TransferBuffer);
    m_Capacity = 0;

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = capacity;
    m_VertexBuffer = SDL_CreateGPUBuffer(m_Device, &bufferInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = capacity;
    m_TransferBuffer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);

    if (!m_VertexBuffer || !m_TransferBuffer) {
        EM_CORE_ERROR("Renderer2D: could not allocate {} byte buffers: {}", capacity,
                      SDL_GetError());
        return false;
    }
    m_Capacity = capacity;
    EM_CORE_TRACE("Renderer2D: vertex buffer grown to {} KiB", capacity / 1024);
    return true;
}

void Renderer2D::Upload(SDL_GPUCommandBuffer* commandBuffer)
{
    End(); // tolerate a missing End()
    m_Uploaded = false;
    if (!m_Pipeline || m_Vertices.empty())
        return;

    const u32 bytes = static_cast<u32>(m_Vertices.size() * sizeof(Vertex));
    if (!EnsureCapacity(bytes))
        return;

    // cycle = true: if the GPU is still reading last frame's data, SDL hands us a fresh
    // internal buffer instead of making us wait (or overwriting data in use).
    void* mapped = SDL_MapGPUTransferBuffer(m_Device, m_TransferBuffer, true);
    if (!mapped) {
        EM_CORE_ERROR("Renderer2D: SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
        return;
    }
    std::memcpy(mapped, m_Vertices.data(), bytes);
    SDL_UnmapGPUTransferBuffer(m_Device, m_TransferBuffer);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commandBuffer);
    const SDL_GPUTransferBufferLocation source{m_TransferBuffer, 0};
    const SDL_GPUBufferRegion destination{m_VertexBuffer, 0, bytes};
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
    m_Uploaded = true;
}

void Renderer2D::Render(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass)
{
    m_LastFrameLines = m_Uploaded ? GetLineCount() : 0;
    if (m_Uploaded) {
        SDL_BindGPUGraphicsPipeline(renderPass, m_Pipeline);
        const SDL_GPUBufferBinding binding{m_VertexBuffer, 0};
        SDL_BindGPUVertexBuffers(renderPass, 0, &binding, 1);
        for (const Batch& batch : m_Batches) {
            if (batch.VertexCount == 0)
                continue;
            // Uniform slot 0 = register(b0, space1) in Renderer2D.vert.hlsl.
            SDL_PushGPUVertexUniformData(commandBuffer, 0, &batch.ViewProjection, sizeof(Mat4));
            SDL_DrawGPUPrimitives(renderPass, batch.VertexCount, 1, batch.FirstVertex, 0);
        }
    }
    Clear();
}

void Renderer2D::Clear()
{
    m_Vertices.clear(); // keeps the capacity, so steady-state frames do not allocate
    m_Batches.clear();
    m_InBatch = false;
    m_Uploaded = false;
}

} // namespace Emerald
