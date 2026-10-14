Emerald Sandbox — Linux x64

Run ./Sandbox.sh (or ./Sandbox). Assets and shaders sit next to the binary. SDL3 is
linked statically. The Vulkan loader, X11, Wayland, ALSA and PulseAudio are loaded
from the system at runtime; a normal Ubuntu desktop or SteamOS install already has
them. `ldd ./Sandbox` only lists libc and libm.

Steam Deck (untested on real hardware): copy this folder over, then in Desktop Mode
choose Games → Add a Non-Steam Game and point Steam at Sandbox.sh. Launch it from
Gaming Mode. See the engine docs (docs/platforms.md in the source tree) for Steam
Input: Gaming Mode usually presents an Xbox-layout pad, which matches the Deck's
A/B/X/Y buttons. This build has not been tried on a physical Steam Deck.
