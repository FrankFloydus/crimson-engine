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
  bool frames_per_second_available = false;
  double frames_per_second = 0.0;
  bool cpu_frame_ms_available = false;
  double cpu_frame_ms = 0.0;
  bool gpu_frame_ms_available = false;
  double gpu_frame_ms = 0.0;
};

} // namespace quader
