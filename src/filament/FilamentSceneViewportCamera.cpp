module;

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/View.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>

module quader.filament.scene_viewport_camera;

import quader.filament.buffer_upload;
import quader.filament.scene_viewport_conversions;
import quader.filament.scene_viewport_lights;
import quader.render.viewport_types;
import quader.render.viewport_projection_policy;

namespace quader {
namespace {

constexpr float kMaximumDynamicLightingFar = 10000.0f;

} // namespace

FilamentViewportCameraResources::FilamentViewportCameraResources(filament::Engine& engine)
    : engine_(engine)
{
}

FilamentViewportCameraResources::~FilamentViewportCameraResources()
{
    destroy();
}

void FilamentViewportCameraResources::create(filament::View& view)
{
    entity_ = utils::EntityManager::get().create();
    camera_ = require_filament_allocation(engine_.createCamera(entity_), "viewport camera");
    attached_view_ = &view;
    attached_view_->setCamera(camera_);
}

void FilamentViewportCameraResources::apply(
    ViewportCamera camera_settings,
    ViewportProjection projection_settings,
    float dynamic_lighting_far,
    RenderExtent target_extent,
    filament::View& view)
{
    camera_state_ = camera_settings;
    projection_state_ = clamp_viewport_projection_for_required_far(
        projection_settings,
        dynamic_lighting_far);

    const filament::math::double3 eye = to_filament_point(camera_state_.position);
    const filament::math::double3 target_point = eye + to_filament_direction(camera_state_.forward);
    camera_->lookAt(eye, target_point, to_filament_up(camera_state_.up));

    apply_projection(target_extent, view);
}

void FilamentViewportCameraResources::apply_projection(RenderExtent target_extent, filament::View& view) const
{
    if (camera_ == nullptr || !target_extent.is_valid()) {
        return;
    }

    const double aspect =
        static_cast<double>(target_extent.width) / static_cast<double>(target_extent.height);

    if (projection_state_.kind == ViewportProjectionKind::orthographic) {
        const double half_height = static_cast<double>(projection_state_.orthographic_size) * 0.5;
        const double half_width = half_height * aspect;
        camera_->setProjection(
            filament::Camera::Projection::ORTHO,
            -half_width,
            half_width,
            -half_height,
            half_height,
            projection_state_.near_plane,
            projection_state_.far_plane);
        view.setDynamicLightingOptions(projection_state_.near_plane, projection_state_.far_plane);
        return;
    }

    camera_->setProjection(
        projection_state_.vertical_fov_degrees,
        aspect,
        projection_state_.near_plane,
        projection_state_.far_plane);
    view.setDynamicLightingOptions(projection_state_.near_plane, projection_state_.far_plane);
}

void FilamentViewportCameraResources::destroy()
{
    if (attached_view_ != nullptr) {
        attached_view_->setCamera(nullptr);
        attached_view_ = nullptr;
    }

    if (entity_) {
        engine_.destroyCameraComponent(entity_);
        utils::EntityManager::get().destroy(entity_);
        entity_.clear();
    }

    camera_ = nullptr;
}

float filament_dynamic_lighting_far_for(
    const SceneRenderSnapshot& snapshot,
    const ViewportCamera& camera,
    const ViewportProjection& projection)
{
    const ViewportProjection safe_projection = clamp_viewport_projection(projection);
    float far_distance = std::max(safe_projection.near_plane + 0.001f, safe_projection.far_plane);
    for (const SceneRenderPointLight& light : snapshot.point_lights) {
        if (!light.entity_id.is_valid()) {
            continue;
        }

        const PointLightSettings settings = clamp_point_light_settings(light.settings);
        const float distance_to_light = distance_between(camera.position, light.position);
        if (!std::isfinite(distance_to_light)) {
            continue;
        }

        far_distance = std::max(
            far_distance,
            distance_to_light + filament_point_light_falloff_radius(settings));
    }

    far_distance = std::clamp(
        far_distance,
        safe_projection.near_plane + 0.001f,
        kMaximumDynamicLightingFar);
    return clamp_viewport_projection_for_required_far(safe_projection, far_distance).far_plane;
}

} // namespace quader
