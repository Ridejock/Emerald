# Emscripten spike

A browser build of the sandbox is **not shippable**. This note is the spike: what was checked
against the SDL version Emerald pins, and what blocks a web build.

[Back to the README](../README.md)

## What was checked

Emerald renders only through SDL GPU (`SDL_CreateGPUDevice` and the rest of `Emerald::Renderer`).
The dependency is SDL **3.4.16** (`release-3.4.16`, URL + SHA256 in `cmake/Dependencies.cmake`).

That tag's GPU sources are three backends and nothing else:

- `src/gpu/vulkan`
- `src/gpu/d3d12`
- `src/gpu/metal`

`SDL_gpu.h` in the same tag defines these shader formats and no others:

- `SDL_GPU_SHADERFORMAT_PRIVATE`
- `SDL_GPU_SHADERFORMAT_SPIRV` (Vulkan)
- `SDL_GPU_SHADERFORMAT_DXBC` and `SDL_GPU_SHADERFORMAT_DXIL` (Direct3D 12)
- `SDL_GPU_SHADERFORMAT_MSL` and `SDL_GPU_SHADERFORMAT_METALLIB` (Metal)

There is no WebGPU backend and no `SDL_GPU_SHADERFORMAT_WGSL` (or any other web format) in this
release. A WebGPU SDL GPU backend has been proposed upstream and is not in 3.4.16:

- [SDL pull request #12046](https://github.com/libsdl-org/SDL/pull/12046) (earlier WebGPU backend, not merged)
- [SDL pull request #16020](https://github.com/libsdl-org/SDL/pull/16020) (experimental WebGPU backend, not merged)

Those patches expect WGSL. Emerald's shaders are HLSL, compiled at build time by SDL_shadercross
to SPIR-V, DXIL and MSL (`cmake/Shaders.cmake`). The pinned shadercross recipe has no WGSL target,
and the Linux CI package builds SPIR-V only. Even after a WebGPU backend landed, the shader step
would have to grow a browser format before the sandbox could draw.

SDL3 on Emscripten can still create a window and use the older `SDL_Renderer` (WebGL). Emerald
does not call that API, and the ImGui backend in use is `imgui_impl_sdlgpu3`, which also needs
SDL GPU.

Configuring with an Emscripten toolchain stops in CMake (`EMSCRIPTEN` / `CMAKE_SYSTEM_NAME`
equal to `Emscripten`) with a fatal error that points here. That is intentional: a partial
configure would fetch the same SDL and then fail inside the GPU device with no backend to select.

## What a later port would need

- An SDL GPU WebGPU backend that is actually in the pinned SDL release, not an open pull request.
- A shader format the browser accepts (WGSL from shadercross, or an equivalent offline step), and
  `EMERALD_SHADER_FORMATS` extended past SPIR-V / DXIL / MSL.
- Audio through SDL's Emscripten driver (WebAudio), and saves somewhere other than a native
  filesystem. XDG paths do not exist in the browser; the save folder would be something like
  IDBFS, which `Paths::GetPrefPath` does not speak.
- Input through the browser Gamepad API. SDL's Emscripten joystick driver can surface that as an
  `SDL_Gamepad`, so the positional bindings could carry over, but that has not been tried. HIDAPI
  (the Steam Deck driver included) does not exist in the browser.

Until those exist, the web demo in the roadmap stays blocked on this spike.
