#pragma once

#include <crimson/renderer/Renderer.hpp>
#include <crimson/renderer/core/NativeContext.hpp>
#include "GraphicsBackend.h"

struct SDL_Window;

namespace crimson {

// The Engine consumes the same renderer class as Quader Public. Only SDL
// window resolution and host resource preparation remain in this adapter.
using Renderer = shared::Renderer;

shared::NativeContext native_renderer_context(SDL_Window* window,
    bgfx::CallbackI* callback = nullptr,
    GraphicsBackend backend = GraphicsBackend::Direct3D12);

} // namespace crimson
