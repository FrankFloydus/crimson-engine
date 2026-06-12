module;

#include <backend/DriverEnums.h>
#include <filament/Engine.h>

#include <exception>
#include <memory>
#include <string>

module quader.filament.render_engine;

import quader.filament.scene_viewport;

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

struct FilamentRenderEngine::Resources {
    Resources() = default;

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        scene_viewport.reset();
        if (engine != nullptr) {
            filament::Engine::destroy(&engine);
        }
    }

    filament::Engine* engine = nullptr;
    std::unique_ptr<FilamentSceneViewport> scene_viewport;
};

FilamentRenderEngine::FilamentRenderEngine()
    : resources_(std::make_unique<Resources>())
{
    try {
        resources_->engine = filament::Engine::create(filament::backend::Backend::VULKAN);
        if (resources_->engine == nullptr) {
            status_.message = "Filament Engine::create returned null.";
            return;
        }

        resources_->scene_viewport = std::make_unique<FilamentSceneViewport>(*resources_->engine);

        status_.initialized = true;
        status_.backend_name = backend_to_string(resources_->engine->getBackend());
        status_.message = "Filament scene viewport renderer is initialized.";
    } catch (const std::exception& error) {
        status_.message = error.what();
    } catch (...) {
        status_.message = "Filament threw an unknown exception during scene viewport initialization.";
    }
}

FilamentRenderEngine::~FilamentRenderEngine() = default;

RendererStatus FilamentRenderEngine::status() const
{
    return status_;
}

bool FilamentRenderEngine::request_scene_viewport_frame(
    const Scene& scene,
    const RenderCamera& camera,
    RenderExtent extent)
{
    if (!status_.initialized || resources_ == nullptr || resources_->scene_viewport == nullptr || !extent.is_valid()) {
        viewport_frame_available_ = false;
        return false;
    }

    resources_->scene_viewport->render_into(scene, camera, extent, viewport_frame_);
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
