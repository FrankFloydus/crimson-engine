module;
#include <tracy/Tracy.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "diagnostics/LogMacros.h"

module quader.app;

import quader.app.render_driver_host;
import quader.diagnostics.logger;

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

} // namespace

Application::Application()
    : sdl_platform_context_()
    , window_("Quader Native", 1280, 800)
    , native_file_dialog_(window_)
    , event_source_(window_)
    , render_driver_(create_default_render_driver_host(window_))
{
    event_dispatcher_.subscribe(input_);

    const RendererStatus status = render_driver_->status();
    QUADER_LOG_INFO(
        "Renderer initialized with {} driver and {} backend.",
        render_driver_kind_display_name(render_driver_->driver_kind()),
        status.backend_name);
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

    return render_driver_->status().initialized ? 0 : 2;
}

void Application::draw_frame(float delta_seconds)
{
    ZoneScopedN("Application frame");

    render_driver_->begin_frame();
    render_editor_dockspace();
    if (scene_properties_.render_frame(open_scene_, native_file_dialog_, selection_)) {
        scene_viewport_.mark_render_state_dirty();
    }
    const EditorViewportFrameResult viewport_result = scene_viewport_.render_frame(
        render_driver_->viewport_renderer(),
        render_driver_->imgui(),
        open_scene_,
        input_,
        selection_,
        hover_,
        commands_,
        delta_seconds);
    if (viewport_result.scene_changed) {
        scene_viewport_.mark_render_state_dirty();
    }
    collect_retired_viewport_gpu_resources();
    render_driver_->end_frame();

    FrameMark;
}

void Application::collect_retired_viewport_gpu_resources()
{
    render_driver_->collect_retired_viewport_gpu_resources(
        scene_viewport_.has_pending_render_requests());
}

void Application::poll_events()
{
    event_source_.poll_events(event_dispatcher_, render_driver_->imgui());
}

void Application::update_relative_mouse_mode()
{
    window_.set_relative_mouse_mode(scene_viewport_.wants_relative_mouse_mode());
}

} // namespace quader
