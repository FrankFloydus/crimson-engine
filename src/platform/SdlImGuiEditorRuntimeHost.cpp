module;

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module quader.app.editor_runtime_host;

import quader.platform.sdl_editor_host_services;
import quader.platform.sdl_imgui_runtime_driver;
import quader.platform.sdl_opengl_runtime_driver;
import quader.platform.sdl_vulkan_runtime_driver;
import quader.platform.sdl_window;
import quader.render.driver_kind;
import quader.render.driver_startup_plan;
import quader.render.status;
import quader.render.viewport_renderer;

namespace quader {
namespace {

[[nodiscard]] SdlWindowGraphicsApi to_sdl_graphics_api(RenderSurfaceKind surface) noexcept
{
    switch (surface) {
    case RenderSurfaceKind::vulkan:
        return SdlWindowGraphicsApi::vulkan;
    case RenderSurfaceKind::opengl:
        return SdlWindowGraphicsApi::opengl;
    case RenderSurfaceKind::metal:
        return SdlWindowGraphicsApi::metal;
    }

    return SdlWindowGraphicsApi::vulkan;
}

[[nodiscard]] SdlWindowDescriptor describe_default_window(RenderSurfaceKind surface) noexcept
{
    return SdlWindowDescriptor {
        .title = "Quader Native",
        .width = 1280,
        .height = 800,
        .graphics_api = to_sdl_graphics_api(surface),
    };
}

[[nodiscard]] SdlWindowDescriptor describe_opengl_window(RenderSurfaceKind surface)
{
    if (surface != RenderSurfaceKind::opengl) {
        throw std::runtime_error("OpenGL render driver requires an OpenGL render surface.");
    }

    return describe_default_window(RenderSurfaceKind::opengl);
}

class SdlImGuiEditorRuntimeHost final
    : public IEditorRuntimeHost
    , public IViewportFramePresenter
    , public IEditorInputState {
public:
    template <typename DriverFactory>
    SdlImGuiEditorRuntimeHost(
        const SdlWindowDescriptor& window_descriptor,
        DriverFactory create_driver)
        : services_(window_descriptor)
        , driver_(create_driver(services_.window()))
    {
        if (!driver_) {
            throw std::invalid_argument("SDL ImGui runtime host driver factory returned null.");
        }
    }

    SdlImGuiEditorRuntimeHost(const SdlImGuiEditorRuntimeHost&) = delete;
    SdlImGuiEditorRuntimeHost& operator=(const SdlImGuiEditorRuntimeHost&) = delete;

    ~SdlImGuiEditorRuntimeHost() override = default;

    [[nodiscard]] RenderDriverKind driver_kind() const noexcept override
    {
        return driver_->driver_kind();
    }

    [[nodiscard]] RendererStatus status() const override
    {
        return driver_->status();
    }

    [[nodiscard]] IEditorShell& editor_shell() override
    {
        return services_.editor_shell();
    }

    [[nodiscard]] IViewportRenderer& viewport_renderer() override
    {
        return driver_->viewport_renderer();
    }

    [[nodiscard]] IViewportFramePresenter& viewport_frame_presenter() override
    {
        return *this;
    }

    [[nodiscard]] IEditorInputState& editor_input_state() override
    {
        return *this;
    }

    [[nodiscard]] IImageFileDialogService& image_file_dialog() override
    {
        return services_.image_file_dialog();
    }

    void poll_events(AppEventDispatcher& dispatcher) override
    {
        services_.poll_events(dispatcher, driver_->native_event_sink());
    }

    void set_relative_mouse_mode(bool enabled) override
    {
        services_.set_relative_mouse_mode(enabled);
    }

    void begin_frame() override
    {
        driver_->begin_frame();
    }

    void end_frame() override
    {
        driver_->end_frame();
    }

    void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) override
    {
        driver_->collect_retired_viewport_gpu_resources(viewport_render_requests_pending);
    }

    [[nodiscard]] bool present_viewport_frame(const ViewportRenderFrame& frame) override
    {
        return driver_->present_viewport_frame(frame);
    }

    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const override
    {
        return driver_->viewport_texture(viewport_id);
    }

    [[nodiscard]] bool text_input_active() const override
    {
        return driver_->text_input_active();
    }

    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids) override
    {
        driver_->prune_viewport_textures(live_viewport_ids);
    }

private:
    SdlEditorHostServices services_;
    std::unique_ptr<ISdlImGuiRuntimeDriver> driver_;
};

[[nodiscard]] std::unique_ptr<IEditorRuntimeHost> create_host_for_candidate(
    const RenderDriverStartupCandidate& candidate,
    const EditorRuntimeHostConfig& config)
{
    if (candidate.availability != RenderDriverBuildAvailability::implemented) {
        throw std::runtime_error(describe_render_driver_unavailable(candidate.driver));
    }

    switch (candidate.driver) {
    case RenderDriverKind::vulkan:
        return std::make_unique<SdlImGuiEditorRuntimeHost>(
            describe_default_window(candidate.surface),
            [&config](const SdlWindow& window) {
                return std::make_unique<SdlVulkanRuntimeDriver>(
                    window,
                    config.imgui_ini_file);
            });
    case RenderDriverKind::opengl:
        return std::make_unique<SdlImGuiEditorRuntimeHost>(
            describe_opengl_window(candidate.surface),
            [&config](const SdlWindow& window) {
                return std::make_unique<SdlOpenGlRuntimeDriver>(
                    window,
                    config.imgui_ini_file);
            });
    case RenderDriverKind::metal:
        throw std::runtime_error(describe_render_driver_unavailable(candidate.driver));
    case RenderDriverKind::automatic:
        break;
    }

    throw std::runtime_error("Automatic is not a concrete render driver.");
}

} // namespace

std::unique_ptr<IEditorRuntimeHost> create_editor_runtime_host(
    RenderDriverSelection selection,
    EditorRuntimeHostConfig config)
{
    if (config.imgui_ini_file.empty()) {
        throw std::invalid_argument("Editor runtime host ImGui ini file path is empty.");
    }

    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(selection);
    std::vector<RenderDriverStartupFailure> failures;
    failures.reserve(plan.candidate_count);

    for (const RenderDriverStartupCandidate& candidate : plan.active_candidates()) {
        try {
            return create_host_for_candidate(candidate, config);
        } catch (const std::exception& exception) {
            if (!selection.is_automatic()) {
                throw std::runtime_error(describe_requested_render_driver_failure(
                    candidate.driver,
                    exception.what()));
            }

            failures.push_back(RenderDriverStartupFailure {
                .driver = candidate.driver,
                .message = exception.what(),
            });
        }
    }

    if (selection.is_automatic()) {
        throw std::runtime_error(describe_automatic_render_driver_failure(failures));
    }

    throw std::runtime_error(describe_requested_render_driver_failure(
        selection.requested_driver,
        "No startup candidate was available."));
}

} // namespace quader
