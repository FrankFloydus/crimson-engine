module;

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

module quader.platform.sdl_opengl_runtime_driver;

import quader.platform.sdl_opengl_context;
import quader.platform.sdl_opengl_texture_allocator;
import quader.render.startup_validation;
import quader.ui.imgui_opengl_layer;

namespace quader {
namespace {

std::unique_ptr<IViewportRenderer> create_opengl_renderer_engine_or_throw(
    const IRenderEngineFactory& render_engine_factory,
    SdlOpenGlContext& context,
    IOpenGlTexture2DAllocator& texture_allocator)
{
    context.make_current();
    const OpenGlSharedContextHandle shared_context = context.shared_context_handle();
    // WGL can reject context sharing while the source context is current on another thread.
    context.clear_current();
    std::unique_ptr<IViewportRenderer> render_engine = render_engine_factory.create_opengl_viewport_renderer(
        shared_context,
        texture_allocator);
    context.make_current();
    if (!render_engine) {
        throw std::runtime_error("Renderer engine factory returned null for OpenGL.");
    }
    throw_if_renderer_failed_to_start(render_engine->status());
    return render_engine;
}

} // namespace

class SdlOpenGlRuntimeDriver::Impl final {
public:
    Impl(
        const SdlWindow& window,
        std::string imgui_ini_file,
        const IRenderEngineFactory& render_engine_factory)
        : context(window)
        , texture_allocator(context)
        , viewport_renderer(create_opengl_renderer_engine_or_throw(
              render_engine_factory,
              context,
              texture_allocator))
        , imgui(window, context, std::move(imgui_ini_file))
    {
    }

    SdlOpenGlContext context;
    SdlOpenGlTextureAllocator texture_allocator;
    std::unique_ptr<IViewportRenderer> viewport_renderer;
    ImGuiOpenGlLayer imgui;
};

SdlOpenGlRuntimeDriver::SdlOpenGlRuntimeDriver(
    const SdlWindow& window,
    std::string imgui_ini_file,
    const IRenderEngineFactory& render_engine_factory)
    : impl_(std::make_unique<Impl>(window, std::move(imgui_ini_file), render_engine_factory))
{
}

SdlOpenGlRuntimeDriver::~SdlOpenGlRuntimeDriver() = default;

RenderDriverKind SdlOpenGlRuntimeDriver::driver_kind() const noexcept
{
    return RenderDriverKind::opengl;
}

RendererStatus SdlOpenGlRuntimeDriver::status() const
{
    return impl_->viewport_renderer->status();
}

ISdlNativeEventSink& SdlOpenGlRuntimeDriver::native_event_sink() noexcept
{
    return impl_->imgui;
}

IViewportRenderer& SdlOpenGlRuntimeDriver::viewport_renderer() noexcept
{
    return *impl_->viewport_renderer;
}

void SdlOpenGlRuntimeDriver::begin_frame()
{
    impl_->context.make_current();
    impl_->imgui.begin_frame();
}

void SdlOpenGlRuntimeDriver::end_frame()
{
    impl_->imgui.end_frame();
}

void SdlOpenGlRuntimeDriver::collect_retired_viewport_gpu_resources(
    bool viewport_render_requests_pending)
{
    const bool has_retired_textures =
        impl_->imgui.has_retired_viewport_gpu_textures() ||
        impl_->texture_allocator.has_retired_textures();
    const bool can_collect_render_resources =
        !viewport_render_requests_pending &&
        impl_->viewport_renderer->has_retired_viewport_gpu_resources();
    if (!has_retired_textures && !can_collect_render_resources) {
        return;
    }

    if (impl_->imgui.has_retired_viewport_gpu_textures()) {
        impl_->imgui.collect_retired_viewport_gpu_textures();
    }
    impl_->texture_allocator.collect_retired_textures();

    if (can_collect_render_resources) {
        impl_->viewport_renderer->collect_retired_viewport_gpu_resources();
    }
}

bool SdlOpenGlRuntimeDriver::present_viewport_frame(const ViewportRenderFrame& frame)
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

ViewportTextureView SdlOpenGlRuntimeDriver::viewport_texture(ViewportId viewport_id) const
{
    return impl_->imgui.viewport_texture(viewport_id);
}

bool SdlOpenGlRuntimeDriver::text_input_active() const
{
    return impl_->imgui.text_input_active();
}

void SdlOpenGlRuntimeDriver::prune_viewport_textures(
    std::span<const ViewportId> live_viewport_ids)
{
    impl_->imgui.prune_viewport_textures(live_viewport_ids);
}

} // namespace quader
