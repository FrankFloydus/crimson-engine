module;
#include <SDL3/SDL.h>

#include <cstdint>
#include <stdexcept>
#include <string>

module quader.platform.sdl_window;

namespace quader {

SdlWindow::SdlWindow(const char* title, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }

    window_ = SDL_CreateWindow(
        title,
        width,
        height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (window_ == nullptr) {
        throw std::runtime_error(SDL_GetError());
    }
}

SdlWindow::~SdlWindow()
{
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }

    SDL_Quit();
}

SDL_Window* SdlWindow::native_handle() const
{
    return window_;
}

uint32_t SdlWindow::id() const
{
    return SDL_GetWindowID(window_);
}

bool SdlWindow::is_relative_mouse_mode_enabled() const
{
    return SDL_GetWindowRelativeMouseMode(window_);
}

std::string SdlWindow::vulkan_error() const
{
    return SDL_GetError();
}

void SdlWindow::set_relative_mouse_mode(bool enabled)
{
    if (is_relative_mouse_mode_enabled() == enabled) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(window_, enabled)) {
        throw std::runtime_error(std::string("SDL_SetWindowRelativeMouseMode failed: ") + SDL_GetError());
    }
}

} // namespace quader
