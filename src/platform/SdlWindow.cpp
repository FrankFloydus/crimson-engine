module;
#include <SDL3/SDL.h>

#include <stdexcept>

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

std::string SdlWindow::vulkan_error() const
{
    return SDL_GetError();
}

} // namespace quader
