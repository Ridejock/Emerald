#pragma once

#include <span>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

// Position, rotation and scale of a shape. Points are scaled, then rotated, then moved:
//   world = Position + Rotate(point * Scale, Rotation)
// Rotation is in radians; positive turns +X towards +Y, which is clockwise on screen in the
// y-down pixel space of Mat4::OrthoPixelSpace.
struct Transform2D {
    Vec2 Position{};
    f32 Rotation = 0.0f;
    Vec2 Scale{1.0f, 1.0f};

    [[nodiscard]] Vec2 Apply(const Vec2& point) const;
};

// Batched 2D line renderer: collects lines on the CPU during the frame, uploads them all in one
// copy pass and draws them with one draw call per Begin/End batch (1 px lines, arcade style).
//
// The Application owns one and drives the GPU side; you only record shapes in OnRender2D:
//
//   void OnRender2D(Renderer2D& r) override
//   {
//       r.Begin(Mat4::OrthoPixelSpace(width, height)); // world -> clip space for this batch
//       r.DrawLine({10, 10}, {200, 50}, {1, 1, 1, 1});
//       r.DrawPolygon(shipPoints, color, {.Position = pos, .Rotation = angle});
//       r.DrawCircle({400, 300}, 50, color);
//       r.End();
//       r.Begin(hudProjection, clipRect); ... r.End(); // more batches, other projections or clip
//                                                       // rectangles are fine
//   }
//
// Why the split: SDL GPU uploads need a copy pass, and copy passes cannot run inside a render
// pass. So every frame the Application calls OnRender2D (CPU only), then Upload() before the
// render pass, and Render() inside it (after OnRender, below the ImGui overlay).
class Renderer2D {
public:
    // Layout of one vertex in the GPU buffer; must match `Input` in Renderer2D.vert.hlsl.
    struct Vertex {
        Vec2 Position; // TEXCOORD0
        u32 Color;     // TEXCOORD1: RGBA, 8 bits each (red in the lowest byte)
    };
    // One Begin/End pair: a range of vertices drawn with one view-projection matrix.
    struct Batch {
        Mat4 ViewProjection;
        SDL_Rect Clip{}; // render-target pixels; w or h == 0: no clipping
        u32 FirstVertex = 0;
        u32 VertexCount = 0;
        // Explicit padding to a multiple of Mat4's 16-byte alignment; implicit padding caused by
        // an alignas member triggers MSVC warning C4324 at /W4.
        u32 Padding[2]{};
    };

    Renderer2D() = default;
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Loads the Renderer2D shaders (compiled for every target by emerald_add_shaders) and builds
    // the line pipeline for `colorFormat`. Returns false on failure (logged). Recording shapes
    // works without it (e.g. in tests); Upload/Render then only discard them.
    bool Init(SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat);
    void Shutdown();

    // --- Recording (CPU only) ---
    // `clip` (optional) limits the batch to a rectangle of the render target, in pixels with
    // (0, 0) at the top-left, e.g. to keep a letterboxed playfield out of the black bars.
    void Begin(const Mat4& viewProjection, const SDL_Rect& clip = {});
    void End();

    void DrawLine(const Vec2& a, const Vec2& b, const Vec4& color);
    // Connects consecutive points (and the last one back to the first if `closed`).
    void DrawPolyline(std::span<const Vec2> points, const Vec4& color,
                      const Transform2D& transform = {}, bool closed = false);
    // Closed outline, e.g. a ship or an asteroid defined around its center.
    void DrawPolygon(std::span<const Vec2> points, const Vec4& color,
                     const Transform2D& transform = {});
    void DrawCircle(const Vec2& center, f32 radius, const Vec4& color, u32 segments = 32);
    void DrawRect(const Vec2& topLeft, const Vec2& size, const Vec4& color);

    // --- GPU side (the Application calls these) ---
    // Copies this frame's vertices to the GPU. Must be called outside of any render pass.
    void Upload(SDL_GPUCommandBuffer* commandBuffer);
    // Draws the uploaded batches into `renderPass`, whose target is targetWidth x targetHeight
    // pixels, then clears everything for the next frame.
    void Render(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass, u32 targetWidth,
                u32 targetHeight);
    // Drops everything recorded so far (Render does this too).
    void Clear();

    [[nodiscard]] std::span<const Vertex> GetVertices() const { return m_Vertices; }
    [[nodiscard]] std::span<const Batch> GetBatches() const { return m_Batches; }
    // Lines recorded so far this frame / drawn by the last Render (for stats overlays).
    [[nodiscard]] u32 GetLineCount() const { return static_cast<u32>(m_Vertices.size() / 2); }
    [[nodiscard]] u32 GetLastFrameLineCount() const { return m_LastFrameLines; }

    // RGBA floats in [0, 1] -> the packed vertex color.
    [[nodiscard]] static u32 PackColor(const Vec4& color);

private:
    [[nodiscard]] bool CanDraw() const;
    bool EnsureCapacity(u32 bytes);

    std::vector<Vertex> m_Vertices;
    std::vector<Batch> m_Batches;
    bool m_InBatch = false;
    bool m_Uploaded = false;
    u32 m_LastFrameLines = 0;

    SDL_GPUDevice* m_Device = nullptr;
    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr;
    // GPU vertex buffer + the CPU-writable transfer buffer used to fill it. Both grow (never
    // shrink) to fit the largest frame so far.
    SDL_GPUBuffer* m_VertexBuffer = nullptr;
    SDL_GPUTransferBuffer* m_TransferBuffer = nullptr;
    u32 m_Capacity = 0; // bytes
};

} // namespace Emerald
