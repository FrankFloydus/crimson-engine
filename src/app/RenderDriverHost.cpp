module;

#include <memory>
#include <optional>
#include <span>

module quader.app.render_driver_host;

import quader.platform.sdl_window;
import quader.app.viewport_frame_presenter;
import quader.render.driver_kind;
import quader.render.engine_factory;
import quader.render.startup_validation;
import quader.render.status;
import quader.render.vulkan_device;
import quader.render.vulkan_swapchain;
import quader.render.viewport_renderer;
import quader.ui.imgui_layer;

namespace quader {
namespace {

std::unique_ptr<IViewportRenderer> create_renderer_engine_or_throw(const VulkanDevice& device)
{
    std::unique_ptr<IViewportRenderer> render_engine = create_default_viewport_renderer(
        device.shared_device_context());
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

class VulkanRenderDriverHost final : public IRenderDriverHost, public IViewportFramePresenter {
public:
    explicit VulkanRenderDriverHost(const SdlWindow& window)
        : window_(window)
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

    [[nodiscard]] IViewportRenderer& viewport_renderer() override
    {
        return *viewport_renderer_;
    }

    [[nodiscard]] IViewportFramePresenter& viewport_frame_presenter() override
    {
        return *this;
    }

    [[nodiscard]] ImGuiLayer& imgui() override
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
    const SdlWindow& window_;
    VulkanDevice device_;
    VulkanSwapchain swapchain_;
    std::unique_ptr<IViewportRenderer> viewport_renderer_;
    ImGuiLayer imgui_;
};

} // namespace

std::unique_ptr<IRenderDriverHost> create_default_render_driver_host(const SdlWindow& window)
{
    return std::make_unique<VulkanRenderDriverHost>(window);
}

} // namespace quader
