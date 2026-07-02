////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

namespace quader {

/**
 * Enumerates ViewportRenderMode values used by the render layer.
 */
enum class ViewportRenderMode {
    TexturedUnlit,
    LitPreview,
};

/**
 * Represents an Environment Document value used by the render extraction and Filament resource layer.
 */
struct EnvironmentDocument {
  QVec3 clear_color = {0.051f, 0.051f, 0.051f};
  float clear_alpha = 1.0f;
};

/**
 * Represents a Render Settings value used by the render extraction and Filament resource layer.
 */
struct RenderSettings {
  ViewportRenderMode viewport_mode = ViewportRenderMode::TexturedUnlit;
  EnvironmentDocument environment;
};

} // namespace quader
