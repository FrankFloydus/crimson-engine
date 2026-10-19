#include "Renderer.h"
#include "Environment.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>

namespace crimson {

shared::NativeContext native_renderer_context(SDL_Window* window,
    bgfx::CallbackI* callback, GraphicsBackend backend) {
    if (!window) throw std::invalid_argument("Renderer requires an SDL window.");
    if (!validGraphicsBackend(backend)) throw std::invalid_argument("Unsupported graphics backend.");
    void* native_window=SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
    if (!native_window) throw std::runtime_error(std::string("SDL did not provide a native HWND: ")+SDL_GetError());
    int width=1,height=1;
    if (!SDL_GetWindowSizeInPixels(window,&width,&height))
        throw std::runtime_error(std::string("Unable to query window pixels: ")+SDL_GetError());
    shared::NativeContext context;
    context.native_window=native_window;
    context.width=static_cast<std::uint32_t>(std::max(1,width));
    context.height=static_cast<std::uint32_t>(std::max(1,height));
    context.callback=callback;
    context.backend=backend==GraphicsBackend::Direct3D11
        ? shared::NativeBackend::Direct3D11 : shared::NativeBackend::Direct3D12;
    auto services=std::make_shared<ForwardServices>();
    services->prepare_environment=prepareEnvironmentCpu;
    context.forward_services=std::move(services);
    return context;
}

} // namespace crimson
