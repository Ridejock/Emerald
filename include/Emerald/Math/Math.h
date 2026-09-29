#pragma once

// Umbrella header for Emerald's math library (header-only):
//   Common.h - constants (Pi), ToRadians/ToDegrees, Clamp, Lerp, NearlyEqual, SIMD config
//   Vec2.h   - Vec2 (float) and Vec2i (int)
//   Vec3.h   - Vec3 with Dot/Cross/Normalize
//   Vec4.h   - Vec4, SSE-accelerated when EMERALD_MATH_SIMD is on
//   Mat4.h   - column-major Mat4: transforms, Ortho/Perspective/LookAt, Transpose, Inverse
#include "Emerald/Math/Common.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec3.h"
#include "Emerald/Math/Vec4.h"
