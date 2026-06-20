#pragma once

#include "quader/render/RenderSettings.h"

#include <string_view>

namespace quader {

enum class RendererBackend {
    Vulkan,
    OpenGL,
    Metal
};

struct RenderConfig {
    RendererBackend backend = RendererBackend::Vulkan;
    RenderSettings settings;
};

inline const char* toString(RendererBackend backend) {
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
