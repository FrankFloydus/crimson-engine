module;

export module quader.render.viewport_projection_policy;

export import quader.render.viewport_types;

export namespace quader {

struct ViewportProjectionLimits {
    float minimum_near_plane = 0.1f;
    float minimum_far_gap = 0.001f;
    float maximum_far_to_near_ratio = 100000.0f;
    float minimum_vertical_fov_degrees = 1.0f;
    float maximum_vertical_fov_degrees = 120.0f;
    float minimum_orthographic_size = 0.001f;
    float maximum_orthographic_size = 100000.0f;
};

[[nodiscard]] ViewportProjectionLimits viewport_projection_limits() noexcept;
[[nodiscard]] float viewport_projection_far_to_near_ratio(ViewportProjection projection) noexcept;
[[nodiscard]] ViewportProjection clamp_viewport_projection(ViewportProjection projection);
[[nodiscard]] ViewportProjection clamp_viewport_projection_for_required_far(
    ViewportProjection projection,
    float required_far_plane);

} // namespace quader
