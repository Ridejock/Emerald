#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Animation.h"
#include "Emerald/Renderer/Camera2D.h"
#include "Emerald/Renderer/Font.h"
#include "Emerald/Renderer/Sprite.h"

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

// Batched 2D renderer for lines (1 px, arcade style) and textured sprites: collects everything on
// the CPU during the frame, uploads it all in one copy pass and draws it with as few draw calls as
// possible.
//
// The Application owns one and drives the GPU side; you only record shapes in OnRender2D:
//
//   void OnRender2D(Renderer2D& r) override
//   {
//       r.Begin(Mat4::OrthoPixelSpace(width, height)); // world -> clip space for this batch
//       r.DrawSprite(background, {640, 360});
//       r.DrawLine({10, 10}, {200, 50}, {1, 1, 1, 1});
//       r.DrawPolygon(shipPoints, color, {.Position = pos, .Rotation = angle});
//       r.DrawSprite(atlas.Get("ship"), pos, {.Rotation = angle});
//       r.DrawCircle({400, 300}, 50, color);
//       r.End();
//       r.Begin(hudProjection, clipRect); ... r.End(); // more batches, other projections or clip
//                                                       // rectangles are fine
//       r.Begin(camera); ... r.End(); // a Camera2D's view, clipped to its viewport
//   }
//
// Layering: everything is drawn in call order (later calls on top), lines and sprites mixed.
// Consecutive draws of the same kind - lines, or sprites from the same texture - share one draw
// call, so group sprites by texture (e.g. use one atlas) to keep the number of draw calls low.
// Draws use straight alpha blending, or additive blending after SetBlendMode(Additive); there is
// no depth buffer.
//
// Why the split: SDL GPU uploads need a copy pass, and copy passes cannot run inside a render
// pass. So every frame the Application calls OnRender2D (CPU only), then Upload() before the
// render pass, and Render() inside it (after OnRender, below the ImGui overlay).
// How a draw combines with what is already on screen.
enum class BlendMode : u8 {
    Alpha,    // the usual "over": out = src * a + dst * (1 - a)
    Additive, // light adds up: out = src * a + dst; overlaps glow brighter (sparks, fire)
};

class Renderer2D {
public:
    // Layout of one vertex in the GPU buffer; must match `Input` in Renderer2D.vert.hlsl.
    struct Vertex {
        Vec2 Position; // TEXCOORD0
        u32 Color;     // TEXCOORD1: RGBA, 8 bits each (red in the lowest byte)
    };
    // Layout of one sprite vertex; must match `Input` in Sprite.vert.hlsl. Six per sprite (two
    // triangles).
    struct SpriteVertex {
        Vec2 Position; // TEXCOORD0
        Vec2 TexCoord; // TEXCOORD1: 0..1 across the texture
        u32 Color;     // TEXCOORD2: tint, packed like Vertex::Color
    };
    enum class CommandType : u8 { Lines, Sprites };
    // A run of consecutive draws of one kind: one draw call.
    struct DrawCommand {
        CommandType Type = CommandType::Lines;
        BlendMode Blend = BlendMode::Alpha;
        u32 FirstVertex = 0; // into GetVertices() (lines) or GetSpriteVertices() (sprites)
        u32 VertexCount = 0;
        // Sprites only: the texture they all use.
        u32 TextureId = 0;
        SDL_GPUTexture* GpuTexture = nullptr;
        SDL_GPUSampler* Sampler = nullptr;
    };
    // One Begin/End pair: its commands, drawn with one view-projection matrix and clip rectangle.
    // (Size is a multiple of Mat4's 16-byte alignment; implicit padding caused by an alignas
    // member would trigger MSVC warning C4324 at /W4.)
    struct Batch {
        Mat4 ViewProjection;
        SDL_Rect Clip{};     // render-target pixels; w or h == 0: no clipping
        u32 FirstVertex = 0; // the batch's line vertices (contiguous in GetVertices())
        u32 VertexCount = 0;
        u32 FirstCommand = 0; // into GetCommands()
        u32 CommandCount = 0;
    };

    Renderer2D() = default;
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Loads the Renderer2D and Sprite shaders (compiled for every target by emerald_add_shaders)
    // and builds the line and sprite pipelines for `colorFormat`. Returns false on failure
    // (logged). Recording shapes works without it (e.g. in tests); Upload/Render then only discard
    // them.
    bool Init(SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat);
    void Shutdown();

    // --- Recording (CPU only) ---
    // `clip` (optional) limits the batch to a rectangle of the render target, in pixels with
    // (0, 0) at the top-left, e.g. to keep a letterboxed playfield out of the black bars.
    void Begin(const Mat4& viewProjection, const SDL_Rect& clip = {});
    // The camera's view-projection, clipped to its (letterboxed) viewport.
    void Begin(const Camera2D& camera) { Begin(camera.GetViewProjection(), camera.GetClip()); }
    void End();
    // Blending for the following draws of this batch (Begin resets it to Alpha). Each change
    // starts a new draw call, so group additive draws together.
    void SetBlendMode(BlendMode mode) { m_BlendMode = mode; }
    [[nodiscard]] BlendMode GetBlendMode() const { return m_BlendMode; }

    void DrawLine(const Vec2& a, const Vec2& b, const Vec4& color);
    // Connects consecutive points (and the last one back to the first if `closed`).
    void DrawPolyline(std::span<const Vec2> points, const Vec4& color,
                      const Transform2D& transform = {}, bool closed = false);
    // Closed outline, e.g. a ship or an asteroid defined around its center.
    void DrawPolygon(std::span<const Vec2> points, const Vec4& color,
                     const Transform2D& transform = {});
    void DrawCircle(const Vec2& center, f32 radius, const Vec4& color, u32 segments = 32);
    void DrawRect(const Vec2& topLeft, const Vec2& size, const Vec4& color);

    // A textured quad; see SpriteOptions for size, rotation, origin, tint, flipping and pixel
    // snapping. `position` is where the sprite's origin (by default its center) goes.
    void DrawSprite(const Sprite& sprite, const Vec2& position, const SpriteOptions& options = {});
    // The whole texture as a sprite.
    void DrawSprite(const Texture& texture, const Vec2& position,
                    const SpriteOptions& options = {});
    // The animator's current frame (nothing before its first Play).
    void DrawSprite(const Animator& animator, const Vec2& position,
                    const SpriteOptions& options = {});

    // Text in `font` (UTF-8, '\n' = new line) as sprites from the font's atlas, one draw call per
    // run. `position` is the top-left of the first line for TextAlign::Left, its top center or
    // top right for Center / Right (every line is aligned on its own). `scale` resizes the baked
    // glyphs; pixel fonts are snapped to whole units.
    void DrawString(const Font& font, std::string_view text, const Vec2& position,
                    const Vec4& color, f32 scale = 1.0f, TextAlign align = TextAlign::Left);

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
    [[nodiscard]] std::span<const SpriteVertex> GetSpriteVertices() const
    {
        return m_SpriteVertices;
    }
    [[nodiscard]] std::span<const DrawCommand> GetCommands() const { return m_Commands; }
    // Lines recorded so far this frame / drawn by the last Render (for stats overlays).
    [[nodiscard]] u32 GetLineCount() const { return static_cast<u32>(m_Vertices.size() / 2); }
    [[nodiscard]] u32 GetLastFrameLineCount() const { return m_LastFrameLines; }
    [[nodiscard]] u32 GetSpriteCount() const
    {
        return static_cast<u32>(m_SpriteVertices.size() / 6);
    }
    [[nodiscard]] u32 GetLastFrameSpriteCount() const { return m_LastFrameSprites; }
    [[nodiscard]] u32 GetLastFrameDrawCalls() const { return m_LastFrameDrawCalls; }

    // RGBA floats in [0, 1] -> the packed vertex color.
    [[nodiscard]] static u32 PackColor(const Vec4& color);

private:
    // A GPU vertex buffer + the CPU-writable transfer buffer used to fill it. Both grow (never
    // shrink) to fit the largest frame so far.
    struct GpuStream {
        SDL_GPUBuffer* Buffer = nullptr;
        SDL_GPUTransferBuffer* Transfer = nullptr;
        u32 Capacity = 0;      // bytes
        const char* Name = ""; // for log messages

        bool Ensure(SDL_GPUDevice* device, u32 bytes);
        // Copies `bytes` from `data` into the transfer buffer; the caller then records the copy.
        bool Fill(SDL_GPUDevice* device, const void* data, u32 bytes);
        void Release(SDL_GPUDevice* device);
    };

    [[nodiscard]] bool CanDraw() const;
    // Makes the current batch's last command one of `type` (and `texture`), starting a new one
    // if needed, so draws are recorded in call order.
    void UseCommand(CommandType type, const Texture* texture = nullptr);
    void CloseCommand();

    std::vector<Vertex> m_Vertices;
    std::vector<SpriteVertex> m_SpriteVertices;
    std::vector<DrawCommand> m_Commands;
    std::vector<Batch> m_Batches;
    std::vector<PlacedGlyph> m_TextGlyphs; // DrawString's scratch list, reused every call
    bool m_InBatch = false;
    BlendMode m_BlendMode = BlendMode::Alpha;
    bool m_LinesUploaded = false;
    bool m_SpritesUploaded = false;
    u32 m_LastFrameLines = 0;
    u32 m_LastFrameSprites = 0;
    u32 m_LastFrameDrawCalls = 0;

    SDL_GPUDevice* m_Device = nullptr;
    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr; // lines
    SDL_GPUGraphicsPipeline* m_SpritePipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_AdditivePipeline = nullptr; // lines, BlendMode::Additive
    SDL_GPUGraphicsPipeline* m_AdditiveSpritePipeline = nullptr;
    GpuStream m_LineStream{.Name = "line"};
    GpuStream m_SpriteStream{.Name = "sprite"};
};

} // namespace Emerald
