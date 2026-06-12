module;

#include <filament/Engine.h>

#include <memory>

export module quader.filament.preview_scene;

export import quader.render.types;

export namespace quader {

class FilamentPreviewScene {
public:
    FilamentPreviewScene(
        filament::Engine& engine,
        const PbrMaterialSettings& material_settings,
        const DirectionalLightSettings& light_settings);

    FilamentPreviewScene(const FilamentPreviewScene&) = delete;
    FilamentPreviewScene& operator=(const FilamentPreviewScene&) = delete;

    ~FilamentPreviewScene();

    void apply_material_settings(const PbrMaterialSettings& settings);
    void apply_directional_light_settings(const DirectionalLightSettings& settings);
    void render_into(RenderExtent extent, RenderPreviewFrame& frame);

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader
