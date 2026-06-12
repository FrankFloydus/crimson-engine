module;

#include <backend/DriverEnums.h>
#include <filament/Engine.h>

#include <exception>
#include <memory>
#include <string>

module quader.filament.render_engine;

namespace quader {
namespace {

std::string backend_to_string(filament::backend::Backend backend)
{
    switch (backend) {
    case filament::backend::Backend::DEFAULT:
        return "Default";
    case filament::backend::Backend::OPENGL:
        return "OpenGL";
    case filament::backend::Backend::VULKAN:
        return "Vulkan";
    case filament::backend::Backend::METAL:
        return "Metal";
    case filament::backend::Backend::WEBGPU:
        return "WebGPU";
    case filament::backend::Backend::NOOP:
        return "Noop";
    }

    return "Unknown";
}

} // namespace

FilamentRenderEngine::FilamentRenderEngine()
{
    try {
        engine_ = filament::Engine::create(filament::backend::Backend::VULKAN);
        if (engine_ == nullptr) {
            status_.message = "Filament Engine::create returned null.";
            return;
        }

        scene_viewport_ = std::make_unique<FilamentSceneViewport>(*engine_);

        status_.initialized = true;
        status_.backend_name = backend_to_string(engine_->getBackend());
        status_.message = "Filament scene viewport renderer is initialized.";
    } catch (const std::exception& error) {
        status_.message = error.what();
    } catch (...) {
        status_.message = "Filament threw an unknown exception during scene viewport initialization.";
    }
}

FilamentRenderEngine::~FilamentRenderEngine()
{
    scene_viewport_.reset();
    if (engine_ != nullptr) {
        filament::Engine::destroy(&engine_);
    }
}

RendererStatus FilamentRenderEngine::status() const
{
    return status_;
}

bool FilamentRenderEngine::request_scene_viewport_frame(
    const Scene& scene,
    const RenderCamera& camera,
    RenderExtent extent)
{
    if (!status_.initialized || scene_viewport_ == nullptr || !extent.is_valid()) {
        viewport_frame_available_ = false;
        return false;
    }

    scene_viewport_->render_into(scene, camera, extent, viewport_frame_);
    viewport_frame_available_ = viewport_frame_.has_pixels();
    return viewport_frame_available_;
}

const RenderViewportFrame* FilamentRenderEngine::try_consume_scene_viewport_frame()
{
    if (!viewport_frame_available_) {
        return nullptr;
    }

    viewport_frame_available_ = false;
    return &viewport_frame_;
}

} // namespace quader
