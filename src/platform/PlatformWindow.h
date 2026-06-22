////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace quader {

/**
 * Stores the Platform Window Options data contract used by the platform integration layer.
 */
struct PlatformWindowOptions {
    bool borderless = false;
};

/**
 * Owns Platform Window behavior within the platform integration layer.
 */
class PlatformWindow {
public:
    PlatformWindow() = default;
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    bool create(const char* title, int width, int height, PlatformWindowOptions options = {});
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
