module;
#include <tracy/Tracy.hpp>

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

std::unique_ptr<IViewportRenderer> create_renderer_engine_or_throw()
{
    std::unique_ptr<IViewportRenderer> render_engine = create_default_viewport_renderer();
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

} // namespace

Application::Application()
    : window_("Quader Native", 1280, 800)
    , native_file_dialog_(window_)
    , event_source_(window_)
    , vulkan_device_(window_)
    , swapchain_(vulkan_device_, window_)
    , render_engine_(create_renderer_engine_or_throw())
    , open_scene_(create_default_scene())
    , imgui_(window_, vulkan_device_, swapchain_)
{
    event_dispatcher_.subscribe(input_);

    const RendererStatus status = render_engine_->status();
    QUADER_LOG_INFO("Renderer initialized with {} backend.", status.backend_name);
}

int Application::run()
{
    ZoneScopedN("Application run");

    bool running = true;
    const auto autoclose_delay = read_autoclose_delay();
    const auto start_time = std::chrono::steady_clock::now();
    auto previous_frame_time = start_time;

    while (running) {
        const auto frame_time = std::chrono::steady_clock::now();
        const std::chrono::duration<float> delta_seconds = frame_time - previous_frame_time;
        previous_frame_time = frame_time;

        input_.begin_frame();
        poll_events();

        if (input_.close_requested()) {
            running = false;
        }

        if (autoclose_delay.has_value() &&
            std::chrono::steady_clock::now() - start_time >= *autoclose_delay) {
            running = false;
        }

        draw_frame(delta_seconds.count());
        update_relative_mouse_mode();
    }

    return render_engine_->status().initialized ? 0 : 2;
}

void Application::draw_frame(float delta_seconds)
{
    ZoneScopedN("Application frame");

    swapchain_.recreate_if_needed(window_);
    imgui_.begin_frame();
    render_editor_dockspace();
    if (scene_properties_.render_frame(open_scene_, native_file_dialog_)) {
        scene_viewport_.mark_render_state_dirty();
    }
    scene_viewport_.render_frame(
        *render_engine_,
        imgui_,
        open_scene_,
        input_,
        delta_seconds);
    imgui_.end_frame(swapchain_);

    FrameMark;
}

void Application::poll_events()
{
    event_source_.poll_events(event_dispatcher_, imgui_);
}

void Application::update_relative_mouse_mode()
{
    window_.set_relative_mouse_mode(scene_viewport_.wants_relative_mouse_mode());
}

} // namespace quader
