# Third-party notices

Emerald's own code is under the MIT License (see [LICENSE](LICENSE)). The libraries below are
downloaded at configure time (see `cmake/Dependencies.cmake`) and are not part of this repository;
their licenses come with their sources.

| Component | Used for | License |
|---|---|---|
| SDL3 | Window, input, GPU, audio | zlib |
| spdlog (with bundled {fmt}) | Logging | MIT |
| stb (stb_image, stb_image_write, stb_truetype, stb_rect_pack) | Images, fonts | MIT or public domain (choice) |
| nlohmann/json | Texture atlas JSON | MIT |
| dr_libs (dr_mp3) | MP3 decoding | MIT-0 or public domain (choice) |
| Dear ImGui (optional) | Debug UI | MIT |
| EnTT (optional) | Entity-component system | MIT |
| SDL_shadercross (build-time tool) | Shader compilation | zlib |

## Bundled with the sandbox (demo asset only)

The engine library ships no fonts or other assets. The sandbox's text demo bundles:

- **Press Start 2P** by CodeMan38 (`sandbox/assets/fonts/PressStart2P-Regular.ttf`),
  Copyright 2012 The Press Start 2P Project Authors (cody@zone38.net), with Reserved Font Name
  "Press Start 2P". Licensed under the SIL Open Font License, Version 1.1; the full license text is
  in [`sandbox/assets/fonts/PressStart2P-OFL.txt`](sandbox/assets/fonts/PressStart2P-OFL.txt).
  Source: https://github.com/google/fonts/tree/main/ofl/pressstart2p
