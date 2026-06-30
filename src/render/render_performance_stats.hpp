////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/**
 * Stores completed-frame timing data exposed by the renderer to editor UI surfaces.
 */
struct RenderFramePerformanceStats {
    bool cpuFrameMsAvailable = false;
    double cpuFrameMs = 0.0;
    bool gpuFrameMsAvailable = false;
    double gpuFrameMs = 0.0;
};

} // namespace quader

