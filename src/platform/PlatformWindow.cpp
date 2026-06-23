////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/PlatformWindow.h"

#include "core/Logger.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace quader {
namespace {

int gSdlVideoWindowCount = 0;

bool acquireSdlVideo() {
    if (gSdlVideoWindowCount == 0 && !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    ++gSdlVideoWindowCount;
    return true;
}

void releaseSdlVideo() {
    if (gSdlVideoWindowCount <= 0) {
        return;
    }

    --gSdlVideoWindowCount;
    if (gSdlVideoWindowCount == 0) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }
}

} // namespace

PlatformWindow::~PlatformWindow() {
    destroy();
}

bool PlatformWindow::create(const char* title, int width, int height, PlatformWindowOptions options) {
    if (!acquireSdlVideo()) {
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (options.borderless) {
        flags |= SDL_WINDOW_BORDERLESS;
        LOG_INFO("Creating borderless platform window");
    }

    window_ = SDL_CreateWindow(
            title,
            width,
            height,
            flags);

    if (!window_) {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        releaseSdlVideo();
        return false;
    }

    refreshPixelSize();
    return true;
}

void PlatformWindow::destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        releaseSdlVideo();
    }
}

void* PlatformWindow::nativeWindowHandle() const {
    if (!window_) {
        return nullptr;
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(window_);
#if defined(_WIN32)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return nullptr;
#endif
}

void PlatformWindow::refreshPixelSize() {
    if (!window_) {
        return;
    }

    int w = 1;
    int h = 1;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    widthPixels_ = static_cast<std::uint32_t>(w > 0 ? w : 1);
    heightPixels_ = static_cast<std::uint32_t>(h > 0 ? h : 1);
}

} // namespace quader
