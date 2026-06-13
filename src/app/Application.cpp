module;
#include <tracy/Tracy.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "diagnostics/LogMacros.h"

module quader.app;

import quader.app.editor_runtime_host;
import quader.diagnostics.logger;
import quader.render.driver_startup_plan;

namespace quader {
namespace {

constexpr RenderDriverKind kRenderDriverMenuDrivers[] {
    RenderDriverKind::automatic,
    RenderDriverKind::vulkan,
    RenderDriverKind::opengl,
    RenderDriverKind::metal,
};

constexpr std::string_view kRenderDriverNotImplementedReason =
    "This render driver is not implemented in this build.";

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

[[nodiscard]] bool render_driver_selection_requires_restart(
    RenderDriverSelection selection,
    RenderDriverKind active_driver) noexcept
{
    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(selection);
    for (const RenderDriverStartupCandidate& candidate : plan.active_candidates()) {
        if (candidate.availability == RenderDriverBuildAvailability::implemented) {
            return candidate.driver != active_driver;
        }
    }

    return true;
}

[[nodiscard]] bool render_driver_can_be_selected(RenderDriverKind driver) noexcept
{
    return driver == RenderDriverKind::automatic || render_driver_is_implemented_in_this_build(driver);
}

[[nodiscard]] EditorMainMenuInput::RenderDriverMenuOption make_render_driver_menu_option(
    RenderDriverKind driver,
    RenderDriverSelection saved_selection) noexcept
{
    const bool enabled = render_driver_can_be_selected(driver);
    return EditorMainMenuInput::RenderDriverMenuOption {
        .kind = driver,
        .label = render_driver_kind_display_name(driver),
        .selected = saved_selection.requested_driver == driver,
        .enabled = enabled,
        .disabled_reason = enabled ? std::string_view {} : kRenderDriverNotImplementedReason,
    };
}

} // namespace

Application::Application(
    UserSettingsPaths user_settings_paths,
    const IRenderEngineFactory& render_engine_factory)
    : user_settings_paths_(std::move(user_settings_paths))
    , render_driver_settings_(std::string(user_settings_paths_.render_driver_settings_file()))
    , render_driver_selection_(render_driver_settings_.load_selection())
    , render_driver_restart_required_(false)
    , runtime_host_(create_editor_runtime_host(
          render_driver_selection_,
          EditorRuntimeHostConfig {
              .imgui_ini_file = std::string(user_settings_paths_.imgui_ini_file()),
          },
          render_engine_factory))
{
    event_dispatcher_.subscribe(input_);

    const RendererStatus status = runtime_host_->status();
    QUADER_LOG_INFO(
        "Renderer initialized with {} driver, {} renderer engine, and {} graphics backend.",
        render_driver_kind_display_name(runtime_host_->driver_kind()),
        status.engine_name,
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

    return runtime_host_->status().initialized ? 0 : 2;
}

void Application::draw_frame(float delta_seconds)
{
    ZoneScopedN("Application frame");

    runtime_host_->begin_frame();
    IImageFileDialogService& image_file_dialog = runtime_host_->image_file_dialog();
    bool render_state_changed = scene_properties_.begin_frame(open_scene_, image_file_dialog, selection_);
    const EditorUiFrameResult editor_ui_result =
        runtime_host_->editor_shell().draw_frame(build_editor_ui_input());
    render_state_changed |= apply_editor_ui_result(editor_ui_result);
    if (render_state_changed) {
        scene_viewport_.mark_render_state_dirty();
    }
    const EditorViewportFrameResult viewport_result = scene_viewport_.render_frame(
        runtime_host_->viewport_renderer(),
        runtime_host_->viewport_frame_presenter(),
        runtime_host_->editor_input_state(),
        runtime_host_->editor_shell(),
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
    runtime_host_->end_frame();

    FrameMark;
}

EditorUiFrameInput Application::build_editor_ui_input()
{
    const RenderDriverKind active_driver = runtime_host_->driver_kind();
    const std::span<const EditorMainMenuInput::RenderDriverMenuOption> render_driver_options =
        build_render_driver_menu_options();

    return EditorUiFrameInput {
        .main_menu = EditorMainMenuInput {
            .active_render_driver_label = render_driver_kind_display_name(active_driver),
            .render_driver_options = render_driver_options,
            .restart_required = render_driver_restart_required_,
        },
        .scene_properties = scene_properties_.build_frame_input(
            open_scene_,
            runtime_host_->image_file_dialog(),
            selection_,
            runtime_host_->viewport_renderer().capabilities()),
    };
}

std::span<const EditorMainMenuInput::RenderDriverMenuOption>
Application::build_render_driver_menu_options()
{
    render_driver_menu_option_count_ = 0;

    for (const RenderDriverKind driver : kRenderDriverMenuDrivers) {
        if (render_driver_menu_option_count_ >= render_driver_menu_options_.size()) {
            break;
        }

        render_driver_menu_options_[render_driver_menu_option_count_] =
            make_render_driver_menu_option(driver, render_driver_selection_);
        ++render_driver_menu_option_count_;
    }

    return {
        render_driver_menu_options_.data(),
        render_driver_menu_option_count_,
    };
}

bool Application::apply_editor_ui_result(const EditorUiFrameResult& result)
{
    bool render_state_changed = apply_main_menu_result(result.main_menu);
    render_state_changed |= scene_properties_.apply_frame_result(
        result.scene_properties,
        open_scene_,
        runtime_host_->image_file_dialog(),
        selection_);
    return render_state_changed;
}

bool Application::apply_main_menu_result(const EditorMainMenuResult& menu)
{
    bool render_state_changed = false;
    if (menu.requested_create.has_value()) {
        render_state_changed |= scene_properties_.apply_create_request(
            *menu.requested_create,
            open_scene_,
            selection_);
    }

    if (menu.requested_render_driver.has_value()) {
        save_render_driver_selection(*menu.requested_render_driver);
    }

    return render_state_changed;
}

void Application::save_render_driver_selection(RenderDriverKind requested_driver)
{
    const RenderDriverSelection selection {
        .requested_driver = requested_driver,
    };

    try {
        render_driver_settings_.save_selection(selection);
    } catch (const std::exception& exception) {
        QUADER_LOG_ERROR("Failed to save render driver preference: {}", exception.what());
        return;
    }

    render_driver_selection_ = selection;
    render_driver_restart_required_ = render_driver_selection_requires_restart(
        render_driver_selection_,
        runtime_host_->driver_kind());

    QUADER_LOG_INFO(
        "Render driver preference saved as {}. Restart required: {}.",
        render_driver_kind_display_name(render_driver_selection_.requested_driver),
        render_driver_restart_required_ ? "yes" : "no");
}

void Application::collect_retired_viewport_gpu_resources()
{
    runtime_host_->collect_retired_viewport_gpu_resources(
        scene_viewport_.has_pending_render_requests());
}

void Application::poll_events()
{
    runtime_host_->poll_events(event_dispatcher_);
}

void Application::update_relative_mouse_mode()
{
    runtime_host_->set_relative_mouse_mode(scene_viewport_.wants_relative_mouse_mode());
}

} // namespace quader
