module;

#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/View.h>
#include <utils/EntityManager.h>

#include <algorithm>

module quader.filament.scene_viewport_lights;

import quader.filament.scene_viewport_conversions;
import quader.render.viewport_types;

namespace quader {
namespace {

constexpr float kMaximumFilamentPointLightLumens = 1.0e12f;
constexpr float kMinimumPointLightRange = 0.001f;

} // namespace

filament::View::ShadowType filament_shadow_type(DirectionalShadowMode mode)
{
    switch (mode) {
    case DirectionalShadowMode::crisp:
        return filament::View::ShadowType::PCF;
    case DirectionalShadowMode::soft:
        return filament::View::ShadowType::PCSS;
    case DirectionalShadowMode::vsm_transparent:
        return filament::View::ShadowType::VSM;
    }

    return filament::View::ShadowType::PCF;
}

filament::LightManager::ShadowOptions filament_shadow_options(const DirectionalShadowSettings& settings)
{
    filament::LightManager::ShadowOptions options;
    options.mapSize = directional_shadow_quality_map_size(settings.quality);
    options.shadowCascades = settings.cascades;
    if (settings.cascades > 1) {
        const float near_distance = settings.near_hint;
        const float far_distance = std::max(settings.far_hint, near_distance + 0.001f);
        filament::LightManager::ShadowCascades::computePracticalSplits(
            options.cascadeSplitPositions,
            settings.cascades,
            near_distance,
            far_distance,
            0.5f);
    }
    options.constantBias = settings.constant_bias;
    options.normalBias = settings.normal_bias;
    options.shadowFar = settings.distance;
    options.shadowNearHint = settings.near_hint;
    options.shadowFarHint = settings.far_hint;
    options.stable = settings.stable;
    options.lispsm = settings.lispsm;
    options.polygonOffsetConstant = settings.polygon_offset_constant;
    options.polygonOffsetSlope = settings.polygon_offset_slope;
    options.screenSpaceContactShadows = settings.contact_shadows;
    options.stepCount = settings.contact_steps;
    options.maxShadowDistance = settings.contact_distance;
    options.vsm.elvsm = settings.vsm_elvsm;
    options.vsm.blurWidth = settings.vsm_blur_width;
    options.shadowBulbRadius = settings.soft_shadow_bulb_radius;
    return options;
}

filament::LightManager::ShadowOptions filament_shadow_options(const PointLightShadowSettings& settings)
{
    filament::LightManager::ShadowOptions options;
    options.mapSize = directional_shadow_quality_map_size(settings.quality);
    options.constantBias = settings.constant_bias;
    options.normalBias = settings.normal_bias;
    return options;
}

float filament_point_light_falloff_radius(PointLightSettings settings)
{
    settings = clamp_point_light_settings(settings);
    return std::max(kMinimumPointLightRange, settings.range);
}

float filament_point_light_lumens(PointLightSettings settings)
{
    settings = clamp_point_light_settings(settings);
    if (settings.falloff_mode == PointLightFalloffMode::inverse_squared) {
        return settings.intensity;
    }

    const PointLightSettings defaults;
    const float reference_range = std::max(kMinimumPointLightRange, defaults.range);
    const float normalized_range = filament_point_light_falloff_radius(settings) / reference_range;
    return std::clamp(
        settings.intensity * normalized_range * normalized_range,
        0.0f,
        kMaximumFilamentPointLightLumens);
}

filament::View::VsmShadowOptions filament_vsm_shadow_options(const DirectionalShadowSettings& settings)
{
    filament::View::VsmShadowOptions options;
    options.highPrecision = settings.vsm_high_precision;
    options.lightBleedReduction = settings.vsm_light_bleed_reduction;
    options.mipmapping = settings.vsm_blur_width > 0.0f;
    return options;
}

filament::View::SoftShadowOptions filament_soft_shadow_options(const DirectionalShadowSettings& settings)
{
    filament::View::SoftShadowOptions options;
    options.penumbraScale = settings.penumbra_scale;
    options.penumbraRatioScale = settings.penumbra_ratio_scale;
    return options;
}

void apply_mesh_shadow_settings(
    filament::Engine& engine,
    utils::Entity mesh_entity,
    const SceneRenderMesh& mesh)
{
    filament::RenderableManager& renderables = engine.getRenderableManager();
    const filament::RenderableManager::Instance renderable_instance = renderables.getInstance(mesh_entity);
    renderables.setCastShadows(renderable_instance, mesh.casts_shadows);
    renderables.setReceiveShadows(renderable_instance, mesh.receives_shadows);
    renderables.setScreenSpaceContactShadows(renderable_instance, mesh.receives_shadows);
}

void apply_directional_light_settings(
    filament::Engine& engine,
    filament::View& view,
    utils::Entity light_entity,
    const SceneRenderDirectionalLight& light)
{
    DirectionalLightSettings settings = light.settings;
    settings = clamp_directional_light_settings(settings);
    view.setShadowType(filament_shadow_type(settings.shadow.mode));
    view.setVsmShadowOptions(filament_vsm_shadow_options(settings.shadow));
    view.setSoftShadowOptions(filament_soft_shadow_options(settings.shadow));

    filament::LightManager& lights = engine.getLightManager();
    const filament::LightManager::Instance light_instance = lights.getInstance(light_entity);
    lights.setColor(light_instance, to_filament_color(settings.color));
    lights.setIntensity(light_instance, settings.intensity);
    lights.setDirection(light_instance, to_filament_float3(light.direction));
    lights.setShadowCaster(light_instance, settings.shadow.enabled);
    lights.setShadowOptions(light_instance, filament_shadow_options(settings.shadow));
}

void apply_point_light_settings(
    filament::Engine& engine,
    utils::Entity light_entity,
    const SceneRenderPointLight& light)
{
    const PointLightSettings settings = clamp_point_light_settings(light.settings);
    const float falloff_radius = filament_point_light_falloff_radius(settings);

    filament::LightManager& lights = engine.getLightManager();
    const filament::LightManager::Instance light_instance = lights.getInstance(light_entity);
    lights.setPosition(light_instance, to_filament_float3(light.position));
    lights.setColor(light_instance, to_filament_color(settings.color));
    lights.setIntensity(light_instance, filament_point_light_lumens(settings));
    lights.setFalloff(light_instance, falloff_radius);
    lights.setShadowCaster(light_instance, settings.shadow.enabled);
    lights.setShadowOptions(light_instance, filament_shadow_options(settings.shadow));
}

} // namespace quader
