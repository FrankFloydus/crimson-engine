////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_settings.hpp"

#include <string_view>

namespace quader {

/**
 * Enumerates RendererBackend values used by the render layer.
 */
enum class RendererBackend {
    Vulkan,
    OpenGL,
    Metal
};

/**
 * Represents a Render Config value used by the render extraction and Filament resource layer.
 */
struct RenderConfig {
    RendererBackend backend = RendererBackend::Vulkan;
    RenderSettings settings;
};

inline const char *to_string(RendererBackend backend) {
  switch (backend) {
  case RendererBackend::Vulkan:
    return "Vulkan";
  case RendererBackend::OpenGL:
    return "OpenGL";
  case RendererBackend::Metal:
    return "Metal";
  }
  return "Unknown";
}

} // namespace quader
