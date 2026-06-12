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

        material_settings_ = clamp_pbr_material_settings(material_settings_);
        light_settings_ = clamp_directional_light_settings(light_settings_);
        preview_scene_ = std::make_unique<FilamentPreviewScene>(*engine_, material_settings_, light_settings_);

        status_.initialized = true;
        status_.backend_name = backend_to_string(engine_->getBackend());
        status_.message = "Filament lit PBR cube preview is initialized.";
    } catch (const std::exception& error) {
        status_.message = error.what();
    } catch (...) {
        status_.message = "Filament threw an unknown exception during preview initialization.";
    }
}

FilamentRenderEngine::~FilamentRenderEngine()
{
    preview_scene_.reset();
    if (engine_ != nullptr) {
        filament::Engine::destroy(&engine_);
    }
}

RendererStatus FilamentRenderEngine::status() const
{
    return status_;
}

PbrMaterialSettings FilamentRenderEngine::material_settings() const
{
    return material_settings_;
}

DirectionalLightSettings FilamentRenderEngine::directional_light_settings() const
{
    return light_settings_;
}

void FilamentRenderEngine::set_material_settings(const PbrMaterialSettings& settings)
{
    material_settings_ = clamp_pbr_material_settings(settings);
    if (preview_scene_ != nullptr) {
        preview_scene_->apply_material_settings(material_settings_);
    }
}

void FilamentRenderEngine::set_directional_light_settings(const DirectionalLightSettings& settings)
{
    light_settings_ = clamp_directional_light_settings(settings);
    if (preview_scene_ != nullptr) {
        preview_scene_->apply_directional_light_settings(light_settings_);
    }
}

bool FilamentRenderEngine::request_debug_preview(RenderExtent extent)
{
    if (!status_.initialized || preview_scene_ == nullptr || !extent.is_valid()) {
        preview_frame_available_ = false;
        return false;
    }

    preview_scene_->render_into(extent, preview_frame_);
    preview_frame_available_ = preview_frame_.has_pixels();
    return preview_frame_available_;
}

const RenderPreviewFrame* FilamentRenderEngine::try_consume_debug_preview_frame()
{
    if (!preview_frame_available_) {
        return nullptr;
    }

    preview_frame_available_ = false;
    return &preview_frame_;
}

} // namespace quader
