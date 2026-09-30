#include "Emerald/Renderer/Pipeline.h"

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

SDL_GPUGraphicsPipeline* CreateGraphicsPipeline(SDL_GPUDevice* device,
                                                const GraphicsPipelineDesc& desc)
{
    // Describe the single color target this pipeline renders into.
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = desc.ColorFormat;
    if (desc.AlphaBlend) {
        // Classic "over" blending: out = src * srcAlpha + dst * (1 - srcAlpha).
        SDL_GPUColorTargetBlendState& blend = colorTarget.blend_state;
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        if (desc.AdditiveBlend) {
            // Light adds up: out = src * srcAlpha + dst. The target's alpha is kept.
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        }
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = desc.VertexShader;
    info.fragment_shader = desc.FragmentShader;

    info.vertex_input_state.vertex_buffer_descriptions = desc.VertexBuffers.data();
    info.vertex_input_state.num_vertex_buffers = static_cast<u32>(desc.VertexBuffers.size());
    info.vertex_input_state.vertex_attributes = desc.VertexAttributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<u32>(desc.VertexAttributes.size());

    info.primitive_type = desc.Primitive;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // draw both windings
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = false;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!pipeline)
        EM_CORE_ERROR("SDL_CreateGPUGraphicsPipeline failed: {}", SDL_GetError());
    return pipeline;
}

} // namespace Emerald
