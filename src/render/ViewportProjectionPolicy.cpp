module;

#include <algorithm>
#include <cmath>
#include <limits>

module quader.render.viewport_projection_policy;

namespace quader {
namespace {

[[nodiscard]] float finite_or(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float safe_maximum_far_plane(float near_plane, float maximum_ratio) noexcept
{
    const float maximum = near_plane * maximum_ratio;
    return std::isfinite(maximum) ? maximum : std::numeric_limits<float>::max();
}

} // namespace

ViewportProjectionLimits viewport_projection_limits() noexcept
{
    return {};
}

float viewport_projection_far_to_near_ratio(ViewportProjection projection) noexcept
{
    if (projection.near_plane <= 0.0f || projection.far_plane <= projection.near_plane) {
        return std::numeric_limits<float>::infinity();
    }

    return projection.far_plane / projection.near_plane;
}

ViewportProjection clamp_viewport_projection(ViewportProjection projection)
{
    return clamp_viewport_projection_for_required_far(projection, projection.far_plane);
}

ViewportProjection clamp_viewport_projection_for_required_far(
    ViewportProjection projection,
    float required_far_plane)
{
    const ViewportProjection defaults;
    const ViewportProjectionLimits limits = viewport_projection_limits();

    projection.vertical_fov_degrees = std::clamp(
        finite_or(projection.vertical_fov_degrees, defaults.vertical_fov_degrees),
        limits.minimum_vertical_fov_degrees,
        limits.maximum_vertical_fov_degrees);
    projection.orthographic_size = std::clamp(
        finite_or(projection.orthographic_size, defaults.orthographic_size),
        limits.minimum_orthographic_size,
        limits.maximum_orthographic_size);
    projection.near_plane = std::max(
        finite_or(projection.near_plane, defaults.near_plane),
        limits.minimum_near_plane);

    const float projection_far = finite_or(projection.far_plane, defaults.far_plane);
    const float requested_far = std::max(
        finite_or(required_far_plane, projection_far),
        projection_far);
    const float minimum_far = projection.near_plane + limits.minimum_far_gap;
    const float maximum_far = safe_maximum_far_plane(
        projection.near_plane,
        limits.maximum_far_to_near_ratio);
    projection.far_plane = std::clamp(
        std::max(requested_far, minimum_far),
        minimum_far,
        maximum_far);

    return projection;
}

} // namespace quader
