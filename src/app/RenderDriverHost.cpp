module;

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module quader.app.render_driver_host;

import quader.platform.sdl_window;
import quader.platform.sdl_opengl_context;
import quader.platform.sdl_opengl_texture_allocator;
import quader.app.viewport_frame_presenter;
import quader.render.driver_kind;
import quader.render.driver_startup_plan;
import quader.render.engine_factory;
import quader.render.startup_validation;
import quader.render.status;
import quader.render.vulkan_device;
import quader.render.vulkan_swapchain;
import quader.render.viewport_renderer;
import quader.ui.imgui_layer;
import quader.ui.imgui_opengl_layer;
import quader.ui_shell.imgui_editor_shell;

namespace quader {
namespace {

std::unique_ptr<IViewportRenderer> create_renderer_engine_or_throw(const VulkanDevice& device)
{
    std::unique_ptr<IViewportRenderer> render_engine = create_default_viewport_renderer(
        device.shared_device_context());
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

std::unique_ptr<IViewportRenderer> create_opengl_renderer_engine_or_throw(
    SdlOpenGlContext& context,
    IOpenGlTexture2DAllocator& texture_allocator)
{
    context.make_current();
    const OpenGlSharedContextHandle shared_context = context.shared_context_handle();
    // WGL can reject context sharing while the source context is current on another thread.
    context.clear_current();
    std::unique_ptr<IViewportRenderer> render_engine = create_opengl_viewport_renderer(
        shared_context,
        texture_allocator);
    context.make_current();
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

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

class VulkanRenderDriverHost final : public IRenderDriverHost, public IViewportFramePresenter {
public:
    explicit VulkanRenderDriverHost(RenderSurfaceKind surface)
        : window_(describe_default_window(surface))
        , device_(window_)
        , swapchain_(device_, window_)
        , viewport_renderer_(create_renderer_engine_or_throw(device_))
        , imgui_(window_, device_, swapchain_)
    {
    }

    VulkanRenderDriverHost(const VulkanRenderDriverHost&) = delete;
    VulkanRenderDriverHost& operator=(const VulkanRenderDriverHost&) = delete;

    ~VulkanRenderDriverHost() override = default;

    [[nodiscard]] RenderDriverKind driver_kind() const noexcept override
    {
        return RenderDriverKind::vulkan;
    }

    [[nodiscard]] RendererStatus status() const override
    {
        return viewport_renderer_->status();
    }

    [[nodiscard]] SdlWindow& window() override
    {
        return window_;
    }

    [[nodiscard]] IEditorShell& editor_shell() override
    {
        return editor_shell_;
    }

    [[nodiscard]] IViewportRenderer& viewport_renderer() override
    {
        return *viewport_renderer_;
    }

    [[nodiscard]] IViewportFramePresenter& viewport_frame_presenter() override
    {
        return *this;
    }

    [[nodiscard]] ISdlNativeEventSink& native_event_sink() override
    {
        return imgui_;
    }

    void begin_frame() override
    {
        swapchain_.recreate_if_needed(window_);
        imgui_.begin_frame();
    }

    void end_frame() override
    {
        imgui_.end_frame(swapchain_);
    }

    void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) override
    {
        const bool has_retired_textures = imgui_.has_retired_viewport_gpu_textures();
        const bool can_collect_render_resources =
            !viewport_render_requests_pending &&
            viewport_renderer_->has_retired_viewport_gpu_resources();
        if (!has_retired_textures && !can_collect_render_resources) {
            return;
        }

        device_.wait_idle();
        if (has_retired_textures) {
            imgui_.collect_retired_viewport_gpu_textures();
        }
        if (can_collect_render_resources) {
            viewport_renderer_->collect_retired_viewport_gpu_resources();
        }
    }

    [[nodiscard]] bool present_viewport_frame(const ViewportRenderFrame& frame) override
    {
        if (!frame.payload.gpu_image.has_value() || !frame.payload.gpu_image->is_ready()) {
            return false;
        }

        std::optional<ViewportSampledGpuImage> image =
            viewport_renderer_->resolve_viewport_gpu_image(*frame.payload.gpu_image);
        if (!image.has_value()) {
            return false;
        }

        return imgui_.present_viewport_gpu_image(*image);
    }

    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const override
    {
        return imgui_.viewport_texture(viewport_id);
    }

    [[nodiscard]] bool text_input_active() const override
    {
        return imgui_.text_input_active();
    }

    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids) override
    {
        imgui_.prune_viewport_textures(live_viewport_ids);
    }

private:
    SdlWindow window_;
    VulkanDevice device_;
    VulkanSwapchain swapchain_;
    std::unique_ptr<IViewportRenderer> viewport_renderer_;
    ImGuiLayer imgui_;
    ImGuiEditorShell editor_shell_;
};

class OpenGlRenderDriverHost final : public IRenderDriverHost, public IViewportFramePresenter {
public:
    explicit OpenGlRenderDriverHost(RenderSurfaceKind surface)
        : window_(describe_opengl_window(surface))
        , context_(window_)
        , texture_allocator_(context_)
        , viewport_renderer_(create_opengl_renderer_engine_or_throw(context_, texture_allocator_))
        , imgui_(window_, context_)
    {
    }

    OpenGlRenderDriverHost(const OpenGlRenderDriverHost&) = delete;
    OpenGlRenderDriverHost& operator=(const OpenGlRenderDriverHost&) = delete;

    ~OpenGlRenderDriverHost() override = default;

    [[nodiscard]] RenderDriverKind driver_kind() const noexcept override
    {
        return RenderDriverKind::opengl;
    }

    [[nodiscard]] RendererStatus status() const override
    {
        return viewport_renderer_->status();
    }

    [[nodiscard]] SdlWindow& window() override
    {
        return window_;
    }

    [[nodiscard]] IEditorShell& editor_shell() override
    {
        return editor_shell_;
    }

    [[nodiscard]] IViewportRenderer& viewport_renderer() override
    {
        return *viewport_renderer_;
    }

    [[nodiscard]] IViewportFramePresenter& viewport_frame_presenter() override
    {
        return *this;
    }

    [[nodiscard]] ISdlNativeEventSink& native_event_sink() override
    {
        return imgui_;
    }

    void begin_frame() override
    {
        context_.make_current();
        imgui_.begin_frame();
    }

    void end_frame() override
    {
        imgui_.end_frame();
    }

    void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) override
    {
        const bool has_retired_textures =
            imgui_.has_retired_viewport_gpu_textures() ||
            texture_allocator_.has_retired_textures();
        const bool can_collect_render_resources =
            !viewport_render_requests_pending &&
            viewport_renderer_->has_retired_viewport_gpu_resources();
        if (!has_retired_textures && !can_collect_render_resources) {
            return;
        }

        if (imgui_.has_retired_viewport_gpu_textures()) {
            imgui_.collect_retired_viewport_gpu_textures();
        }
        texture_allocator_.collect_retired_textures();

        if (can_collect_render_resources) {
            viewport_renderer_->collect_retired_viewport_gpu_resources();
        }
    }

    [[nodiscard]] bool present_viewport_frame(const ViewportRenderFrame& frame) override
    {
        if (!frame.payload.gpu_image.has_value() || !frame.payload.gpu_image->is_ready()) {
            return false;
        }

        std::optional<ViewportSampledGpuImage> image =
            viewport_renderer_->resolve_viewport_gpu_image(*frame.payload.gpu_image);
        if (!image.has_value()) {
            return false;
        }

        return imgui_.present_viewport_gpu_image(*image);
    }

    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const override
    {
        return imgui_.viewport_texture(viewport_id);
    }

    [[nodiscard]] bool text_input_active() const override
    {
        return imgui_.text_input_active();
    }

    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids) override
    {
        imgui_.prune_viewport_textures(live_viewport_ids);
    }

private:
    SdlWindow window_;
    SdlOpenGlContext context_;
    SdlOpenGlTextureAllocator texture_allocator_;
    std::unique_ptr<IViewportRenderer> viewport_renderer_;
    ImGuiOpenGlLayer imgui_;
    ImGuiEditorShell editor_shell_;
};

[[nodiscard]] std::unique_ptr<IRenderDriverHost> create_host_for_candidate(
    const RenderDriverStartupCandidate& candidate)
{
    if (candidate.availability != RenderDriverBuildAvailability::implemented) {
        throw std::runtime_error(describe_render_driver_unavailable(candidate.driver));
    }

    switch (candidate.driver) {
    case RenderDriverKind::vulkan:
        return std::make_unique<VulkanRenderDriverHost>(candidate.surface);
    case RenderDriverKind::opengl:
        return std::make_unique<OpenGlRenderDriverHost>(candidate.surface);
    case RenderDriverKind::metal:
        throw std::runtime_error(describe_render_driver_unavailable(candidate.driver));
    case RenderDriverKind::automatic:
        break;
    }

    throw std::runtime_error("Automatic is not a concrete render driver.");
}

} // namespace

std::unique_ptr<IRenderDriverHost> create_render_driver_host(RenderDriverSelection selection)
{
    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(selection);
    std::vector<RenderDriverStartupFailure> failures;
    failures.reserve(plan.candidate_count);

    for (const RenderDriverStartupCandidate& candidate : plan.active_candidates()) {
        try {
            return create_host_for_candidate(candidate);
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

std::unique_ptr<IRenderDriverHost> create_default_render_driver_host()
{
    return create_render_driver_host(RenderDriverSelection {});
}

} // namespace quader
