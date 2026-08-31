////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/// World-space allowance shared by every viewport candidate domain when it is
/// compared with the canonical render-visible scene surface.
inline constexpr double kViewportSurfaceOcclusionTolerance = 0.001;

} // namespace quader
