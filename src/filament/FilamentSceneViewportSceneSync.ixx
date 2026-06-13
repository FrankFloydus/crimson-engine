module;

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>

#include <memory>

export module quader.filament.scene_viewport_scene_sync;

export import quader.filament.scene_viewport_materials;
export import quader.render.viewport_types;

export namespace quader {

class FilamentSceneSyncResources final {
public:
    FilamentSceneSyncResources(
        filament::Engine& engine,
        filament::Scene& scene,
        filament::View& view,
        FilamentPbrMaterialResources& material_resources);
    FilamentSceneSyncResources(const FilamentSceneSyncResources&) = delete;
    FilamentSceneSyncResources& operator=(const FilamentSceneSyncResources&) = delete;
    ~FilamentSceneSyncResources();

    void apply_snapshot(const SceneRenderSnapshot& snapshot);
    void destroy();

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader
