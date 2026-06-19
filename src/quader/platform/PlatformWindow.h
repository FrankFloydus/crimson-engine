#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace quader {

class PlatformWindow {
public:
    PlatformWindow() = default;
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    bool create(const char* title, int width, int height);
    void destroy();

    SDL_Window* sdlWindow() const { return window_; }
    void* nativeWindowHandle() const;

    std::uint32_t widthPixels() const { return widthPixels_; }
    std::uint32_t heightPixels() const { return heightPixels_; }
    void refreshPixelSize();

private:
    SDL_Window* window_ = nullptr;
    std::uint32_t widthPixels_ = 1;
    std::uint32_t heightPixels_ = 1;
};

} // namespace quader
