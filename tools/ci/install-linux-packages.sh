#!/usr/bin/env bash
# Packages for a Linux game build. Same set as .github/workflows/linux.yml:
# SDL's video and audio dependencies, plus lavapipe and Xvfb so a smoke run
# on software Vulkan has a display.
set -euo pipefail

if [ "$(uname -s)" != Linux ]; then
    exit 0
fi

sudo apt-get update
sudo apt-get install -y ninja-build pkg-config \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
    libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
    mesa-vulkan-drivers xvfb
