////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_input_types.hpp"

#include <cstddef>

namespace quader {

/// Selects a pane-local override of the shared viewport preference defaults.
enum class QuaderViewportControl {
  SnappingEnabled,
  GridSnap,
  VertexSnap,
  RotationSnap,
  ShowGrid,
  ShowGizmos,
  ShowOverlays,
  ShowMeshGrid,
  Count,
};

inline constexpr std::size_t kQuaderViewportControlCount =
    static_cast<std::size_t>(QuaderViewportControl::Count);

} // namespace quader
