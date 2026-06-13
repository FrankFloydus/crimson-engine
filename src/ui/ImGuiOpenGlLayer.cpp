module;
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

module quader.ui.imgui_opengl_layer;

import quader.ui.imgui_backend_common;
import quader.ui.viewport_texture_store;

namespace quader {
namespace {

class ImGuiOpenGlViewportTextureRegistry final : public IViewportTextureRegistry {
public:
    [[nodiscard]] uint64_t add_texture(const ViewportTextureToken& texture) override
    {
        const std::optional<ViewportOpenGlTextureTokenPayload> payload =
            unpack_opengl_viewport_texture_token(texture);
        if (!payload.has_value()) {
            return 0;
        }

        return payload->texture_id;
    }

    void remove_texture(uint64_t texture_id) override
    {
        (void)texture_id;
    }
};

} // namespace

class ImGuiOpenGlLayer::Impl {
public:
    Impl(
        const SdlWindow& window,
        SdlOpenGlContext& context,
        std::string imgui_ini_file,
        const char* glsl_version);

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl();

    void process_sdl_event(const SDL_Event& event);
    void begin_frame();
    void end_frame();
    [[nodiscard]] bool present_viewport_gpu_image(const ViewportSampledGpuImage& image);
    [[nodiscard]] bool has_retired_viewport_gpu_textures() const noexcept;
    void collect_retired_viewport_gpu_textures();
    void remove_viewport_texture(ViewportId viewport_id);
    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids);
    [[nodiscard]] bool text_input_active() const;
    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const;

private:
    void shutdown_backend_state() noexcept;

    SdlOpenGlContext& context_;
    std::string imgui_ini_file_;
    ImGuiOpenGlViewportTextureRegistry texture_registry_;
    std::unique_ptr<ViewportTextureStore> viewport_textures_;
    bool imgui_context_created_ = false;
    bool sdl_backend_initialized_ = false;
    bool opengl_backend_initialized_ = false;
};

ImGuiOpenGlLayer::Impl::Impl(
    const SdlWindow& window,
    SdlOpenGlContext& context,
    std::string imgui_ini_file,
    const char* glsl_version)
    : context_(context)
    , imgui_ini_file_(std::move(imgui_ini_file))
{
    try {
        context_.make_current();

        create_editor_imgui_context(imgui_ini_file_.c_str());
        imgui_context_created_ = true;

        if (!ImGui_ImplSDL3_InitForOpenGL(window.native_handle(), context_.native_handle())) {
            throw std::runtime_error("ImGui SDL3 backend failed to initialize for OpenGL.");
        }
        sdl_backend_initialized_ = true;

        if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
            throw std::runtime_error("ImGui OpenGL3 backend failed to initialize.");
        }
        opengl_backend_initialized_ = true;

        viewport_textures_ = std::make_unique<ViewportTextureStore>(texture_registry_);
    } catch (...) {
        shutdown_backend_state();
        throw;
    }
}

ImGuiOpenGlLayer::Impl::~Impl()
{
    shutdown_backend_state();
}

void ImGuiOpenGlLayer::Impl::shutdown_backend_state() noexcept
{
    (void)context_.try_make_current();

    viewport_textures_.reset();

    if (opengl_backend_initialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        opengl_backend_initialized_ = false;
    }

    if (sdl_backend_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        sdl_backend_initialized_ = false;
    }

    if (imgui_context_created_) {
        ImGui::DestroyContext();
        imgui_context_created_ = false;
    }
}

void ImGuiOpenGlLayer::Impl::process_sdl_event(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiOpenGlLayer::Impl::begin_frame()
{
    context_.make_current();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiOpenGlLayer::Impl::end_frame()
{
    context_.make_current();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    context_.swap_window();
}

bool ImGuiOpenGlLayer::Impl::present_viewport_gpu_image(const ViewportSampledGpuImage& image)
{
    return viewport_textures_ != nullptr && viewport_textures_->present_texture_token(image.texture_token());
}

bool ImGuiOpenGlLayer::Impl::has_retired_viewport_gpu_textures() const noexcept
{
    return viewport_textures_ != nullptr && viewport_textures_->has_retired_gpu_textures();
}

void ImGuiOpenGlLayer::Impl::collect_retired_viewport_gpu_textures()
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->collect_retired_gpu_textures();
    }
}

void ImGuiOpenGlLayer::Impl::remove_viewport_texture(ViewportId viewport_id)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->remove(viewport_id);
    }
}

void ImGuiOpenGlLayer::Impl::prune_viewport_textures(std::span<const ViewportId> live_viewport_ids)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->prune(live_viewport_ids);
    }
}

bool ImGuiOpenGlLayer::Impl::text_input_active() const
{
    return ImGui::GetIO().WantTextInput;
}

ViewportTextureView ImGuiOpenGlLayer::Impl::viewport_texture(ViewportId viewport_id) const
{
    return viewport_textures_ ? viewport_textures_->texture_view(viewport_id) : ViewportTextureView {};
}

ImGuiOpenGlLayer::ImGuiOpenGlLayer(
    const SdlWindow& window,
    SdlOpenGlContext& context,
    std::string imgui_ini_file,
    const char* glsl_version)
    : impl_(std::make_unique<Impl>(
          window,
          context,
          std::move(imgui_ini_file),
          glsl_version))
{
}

ImGuiOpenGlLayer::~ImGuiOpenGlLayer() = default;

void ImGuiOpenGlLayer::process_sdl_event(const SDL_Event& event)
{
    impl_->process_sdl_event(event);
}

void ImGuiOpenGlLayer::begin_frame()
{
    impl_->begin_frame();
}

void ImGuiOpenGlLayer::end_frame()
{
    impl_->end_frame();
}

bool ImGuiOpenGlLayer::present_viewport_gpu_image(const ViewportSampledGpuImage& image)
{
    return impl_->present_viewport_gpu_image(image);
}

bool ImGuiOpenGlLayer::has_retired_viewport_gpu_textures() const noexcept
{
    return impl_->has_retired_viewport_gpu_textures();
}

void ImGuiOpenGlLayer::collect_retired_viewport_gpu_textures()
{
    impl_->collect_retired_viewport_gpu_textures();
}

void ImGuiOpenGlLayer::remove_viewport_texture(ViewportId viewport_id)
{
    impl_->remove_viewport_texture(viewport_id);
}

void ImGuiOpenGlLayer::prune_viewport_textures(std::span<const ViewportId> live_viewport_ids)
{
    impl_->prune_viewport_textures(live_viewport_ids);
}

bool ImGuiOpenGlLayer::text_input_active() const
{
    return impl_->text_input_active();
}

ViewportTextureView ImGuiOpenGlLayer::viewport_texture(ViewportId viewport_id) const
{
    return impl_->viewport_texture(viewport_id);
}

} // namespace quader
