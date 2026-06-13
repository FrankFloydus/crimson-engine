module;

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/View.h>
#include <utils/EntityManager.h>

export module quader.filament.scene_viewport_camera;

export import quader.render.viewport_types;

export namespace quader {

class FilamentViewportCameraResources final {
public:
    explicit FilamentViewportCameraResources(filament::Engine& engine);
    FilamentViewportCameraResources(const FilamentViewportCameraResources&) = delete;
    FilamentViewportCameraResources& operator=(const FilamentViewportCameraResources&) = delete;
    ~FilamentViewportCameraResources();

    void create(filament::View& view);
    void apply(
        ViewportCamera camera_settings,
        ViewportProjection projection_settings,
        float dynamic_lighting_far,
        RenderExtent target_extent,
        filament::View& view);
    void apply_projection(RenderExtent target_extent, filament::View& view) const;
    void destroy();

private:
    filament::Engine& engine_;
    filament::Camera* camera_ = nullptr;
    filament::View* attached_view_ = nullptr;
    utils::Entity entity_;
    ViewportCamera camera_state_;
    ViewportProjection projection_state_;
};

[[nodiscard]] float filament_dynamic_lighting_far_for(
    const SceneRenderSnapshot& snapshot,
    const ViewportCamera& camera,
    const ViewportProjection& projection);

} // namespace quader
