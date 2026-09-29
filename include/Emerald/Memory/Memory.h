#pragma once

// Umbrella header for Emerald's std::pmr based memory helpers:
//   PmrTypes.h         - PmrVector, PmrString, PmrUnorderedMap, ... aliases
//   FrameArena.h       - per-frame bump allocator, reset by Application every frame
//   TrackingResource.h - memory_resource wrapper that counts bytes/allocations
#include "Emerald/Memory/FrameArena.h"
#include "Emerald/Memory/PmrTypes.h"
#include "Emerald/Memory/TrackingResource.h"
