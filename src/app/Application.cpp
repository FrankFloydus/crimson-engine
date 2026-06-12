module;
#include <tracy/Tracy.hpp>
#include <SDL3/SDL.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "diagnostics/LogMacros.h"

module quader.app;

import quader.diagnostics.logger;
import quader.render.engine_factory;
import quader.render.startup_validation;

namespace quader {
namespace {

std::optional<std::chrono::milliseconds> read_autoclose_delay()
{
#if defined(_WIN32)
    char* value = nullptr;
    size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, "QUADER_AUTOCLOSE_MS") != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned_value(value, &std::free);
#else
    const char* value = std::getenv("QUADER_AUTOCLOSE_MS");
    if (value == nullptr) {
        return std::nullopt;
    }
#endif

    try {
        const auto delay = std::stoll(value);
        if (delay > 0) {
            return std::chrono::milliseconds(delay);
        }
    } catch (...) {
    }

    return std::nullopt;
}

std::unique_ptr<IDebugPreviewRenderer> create_renderer_engine_or_throw()
{
    std::unique_ptr<IDebugPreviewRenderer> render_engine = create_default_debug_preview_renderer();
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

bool should_close_for(const SDL_Event& event, const SdlWindow& window)
{
    if (event.type == SDL_EVENT_QUIT) {
        return true;
    }

    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event.window.windowID == SDL_GetWindowID(window.native_handle())) {
        return true;
    }

    return event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE;
}

} // namespace

Application::Application()
    : window_("Quader Native", 1280, 800)
    , vulkan_device_(window_)
    , swapchain_(vulkan_device_, window_)
    , render_engine_(create_renderer_engine_or_throw())
    , imgui_(window_, vulkan_device_, swapchain_)
{
    const RendererStatus status = render_engine_->status();
    QUADER_LOG_INFO("Renderer initialized with {} backend.", status.backend_name);
}

int Application::run()
{
    ZoneScopedN("Application run");

    bool running = true;
    const auto autoclose_delay = read_autoclose_delay();
    const auto start_time = std::chrono::steady_clock::now();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            imgui_.process_event(event);
            if (should_close_for(event, window_)) {
                running = false;
            }
        }

        if (autoclose_delay.has_value() &&
            std::chrono::steady_clock::now() - start_time >= *autoclose_delay) {
            running = false;
        }

        draw_frame();
    }

    return render_engine_->status().initialized ? 0 : 2;
}

void Application::draw_frame()
{
    ZoneScopedN("Application frame");

    swapchain_.recreate_if_needed(window_);
    imgui_.begin_frame();
    preview_controller_.render_frame(*render_engine_, imgui_);
    imgui_.end_frame(swapchain_);

    FrameMark;
}

} // namespace quader
