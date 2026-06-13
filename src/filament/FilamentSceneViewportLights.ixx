module;

#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/View.h>
#include <utils/EntityManager.h>

export module quader.filament.scene_viewport_lights;

export import quader.render.viewport_types;

export namespace quader {

[[nodiscard]] filament::View::ShadowType filament_shadow_type(DirectionalShadowMode mode);
[[nodiscard]] filament::LightManager::ShadowOptions filament_shadow_options(
    const DirectionalShadowSettings& settings);
[[nodiscard]] filament::LightManager::ShadowOptions filament_shadow_options(
    const PointLightShadowSettings& settings);
[[nodiscard]] float filament_point_light_falloff_radius(PointLightSettings settings);
[[nodiscard]] float filament_point_light_lumens(PointLightSettings settings);
[[nodiscard]] filament::View::VsmShadowOptions filament_vsm_shadow_options(
    const DirectionalShadowSettings& settings);
[[nodiscard]] filament::View::SoftShadowOptions filament_soft_shadow_options(
    const DirectionalShadowSettings& settings);

void apply_mesh_shadow_settings(
    filament::Engine& engine,
    utils::Entity mesh_entity,
    const SceneRenderMesh& mesh);

void apply_directional_light_settings(
    filament::Engine& engine,
    filament::View& view,
    utils::Entity light_entity,
    const SceneRenderDirectionalLight& light);

void apply_point_light_settings(
    filament::Engine& engine,
    utils::Entity light_entity,
    const SceneRenderPointLight& light);

} // namespace quader
