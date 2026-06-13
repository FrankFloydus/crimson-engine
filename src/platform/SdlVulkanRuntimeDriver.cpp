module;

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

module quader.platform.sdl_vulkan_runtime_driver;

import quader.platform.sdl_vulkan_presentation_surface;
import quader.render.startup_validation;
import quader.render.vulkan_device;
import quader.render.vulkan_swapchain;
import quader.ui.imgui_layer;

namespace quader {
namespace {

std::unique_ptr<IViewportRenderer> create_vulkan_renderer_engine_or_throw(
    const IRenderEngineFactory& render_engine_factory,
    const VulkanDevice& device)
{
    std::unique_ptr<IViewportRenderer> render_engine = render_engine_factory.create_vulkan_viewport_renderer(
        device.shared_device_context());
    if (!render_engine) {
        throw std::runtime_error("Renderer engine factory returned null for Vulkan.");
    }
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

} // namespace

class SdlVulkanRuntimeDriver::Impl final {
public:
    Impl(
        const SdlWindow& window,
        std::string imgui_ini_file,
        const IRenderEngineFactory& render_engine_factory)
        : presentation_surface(window)
        , device(presentation_surface)
        , swapchain(device, presentation_surface)
        , viewport_renderer(create_vulkan_renderer_engine_or_throw(render_engine_factory, device))
        , imgui(window, device, swapchain, std::move(imgui_ini_file))
    {
    }

    SdlVulkanPresentationSurface presentation_surface;
    VulkanDevice device;
    VulkanSwapchain swapchain;
    std::unique_ptr<IViewportRenderer> viewport_renderer;
    ImGuiLayer imgui;
};

SdlVulkanRuntimeDriver::SdlVulkanRuntimeDriver(
    const SdlWindow& window,
    std::string imgui_ini_file,
    const IRenderEngineFactory& render_engine_factory)
    : impl_(std::make_unique<Impl>(window, std::move(imgui_ini_file), render_engine_factory))
{
}

SdlVulkanRuntimeDriver::~SdlVulkanRuntimeDriver() = default;

RenderDriverKind SdlVulkanRuntimeDriver::driver_kind() const noexcept
{
    return RenderDriverKind::vulkan;
}

RendererStatus SdlVulkanRuntimeDriver::status() const
{
    return impl_->viewport_renderer->status();
}

ISdlNativeEventSink& SdlVulkanRuntimeDriver::native_event_sink() noexcept
{
    return impl_->imgui;
}

IViewportRenderer& SdlVulkanRuntimeDriver::viewport_renderer() noexcept
{
    return *impl_->viewport_renderer;
}

void SdlVulkanRuntimeDriver::begin_frame()
{
    impl_->swapchain.recreate_if_needed(impl_->presentation_surface);
    impl_->imgui.begin_frame();
}

void SdlVulkanRuntimeDriver::end_frame()
{
    impl_->imgui.end_frame(impl_->swapchain);
}

void SdlVulkanRuntimeDriver::collect_retired_viewport_gpu_resources(
    bool viewport_render_requests_pending)
{
    const bool has_retired_textures = impl_->imgui.has_retired_viewport_gpu_textures();
    const bool can_collect_render_resources =
        !viewport_render_requests_pending &&
        impl_->viewport_renderer->has_retired_viewport_gpu_resources();
    if (!has_retired_textures && !can_collect_render_resources) {
        return;
    }

    impl_->device.wait_idle();
    if (has_retired_textures) {
        impl_->imgui.collect_retired_viewport_gpu_textures();
    }
    if (can_collect_render_resources) {
        impl_->viewport_renderer->collect_retired_viewport_gpu_resources();
    }
}

bool SdlVulkanRuntimeDriver::present_viewport_frame(const ViewportRenderFrame& frame)
{
    if (!frame.payload.gpu_image.has_value() || !frame.payload.gpu_image->is_ready()) {
        return false;
    }

    std::optional<ViewportSampledGpuImage> image =
        impl_->viewport_renderer->resolve_viewport_gpu_image(*frame.payload.gpu_image);
    if (!image.has_value()) {
        return false;
    }

    return impl_->imgui.present_viewport_gpu_image(*image);
}

ViewportTextureView SdlVulkanRuntimeDriver::viewport_texture(ViewportId viewport_id) const
{
    return impl_->imgui.viewport_texture(viewport_id);
}

bool SdlVulkanRuntimeDriver::text_input_active() const
{
    return impl_->imgui.text_input_active();
}

void SdlVulkanRuntimeDriver::prune_viewport_textures(
    std::span<const ViewportId> live_viewport_ids)
{
    impl_->imgui.prune_viewport_textures(live_viewport_ids);
}

} // namespace quader
