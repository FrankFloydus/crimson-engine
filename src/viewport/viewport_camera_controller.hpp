////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "viewport/viewport_navigation_defaults.hpp"
#include "editor/interactions/viewport_segment_projection.hpp"

#include <cstdint>
#include <optional>

namespace quader {

/**
 * Enumerates ViewportDragMode values used by the viewport layer.
 */
enum class ViewportDragMode {
    None,
    Orbit,
    OrbitSnap,
    Pan,
    Fly
};

/**
 * Enumerates ViewportViewAxis values used by the viewport layer.
 */
enum class ViewportViewAxis {
    X,
    Y,
    Z,
};

/**
 * Enumerates ViewportAxisLock values used by the viewport layer.
 */
enum class ViewportAxisLock {
    Locked,
    Unlocked,
};

/**
 * Enumerates ViewportProjection values used by the viewport layer.
 */
enum class ViewportProjection {
    Perspective,
    Orthographic,
};

/**
 * Stores the Viewport Camera State data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportCameraState {
    QVec3 target = {};
    float yaw_radians = -0.6435011f;
    float pitch_radians = -0.4636476f;
    float distance = 11.18034f;
    ViewportProjection projection = ViewportProjection::Perspective;
    float orthographic_size = 24.0f;
    float fov_degrees = 60.0f;
    float near_plane = 0.05f;
    float far_plane = 1000.0f;
};

/**
 * Represents a Viewport Camera Pose value used by the viewport input, layout, and camera layer.
 */
struct ViewportCameraPose {
    QVec3 eye;
    QVec3 target;
    QVec3 up;
};

[[nodiscard]] bool
viewport_camera_pose_is_valid(const ViewportCameraPose &pose) noexcept;
[[nodiscard]] std::uint64_t
viewport_camera_pose_persistence_signature(
    const ViewportCameraPose &pose) noexcept;

/**
 * Represents a Viewport Ray value used by the viewport input, layout, and camera layer.
 */
struct ViewportRay {
    QVec3 origin;
    QVec3 direction;
};

/**
 * Represents a Viewport Screen Point value used by the viewport input, layout, and camera layer.
 */
struct ViewportScreenPoint {
    QVec2 position;
    float depth = 0.0f;
    bool visible = false;
};

/**
 * Stores renderer- and scene-neutral bounds used by camera framing decisions.
 */
struct ViewportFocusBounds {
    QVec3 min = {};
    QVec3 max = {};
};

/**
 * Defines the explicit camera response when a focus request has no bounds.
 */
enum class ViewportEmptyFocusBehavior {
    KeepCamera,
    FocusOrigin,
};

/**
 * Coordinates Viewport Camera Controller state transitions for the viewport input, layout, and camera layer.
 */
class ViewportCameraController {
public:
    ViewportCameraController();

    void set_viewport_size(float width, float height);
    void begin_drag(QVec2 mouse, ViewportDragMode mode,
                    std::optional<QVec3> navigation_anchor = std::nullopt,
                    bool navigation_anchor_repair_only = false);
    void update_drag(QVec2 mouse);
    void update_drag(QVec2 mouse, ViewportDragMode mode);
    void end_drag();
    void scroll(float wheel_delta,
                std::optional<QVec3> navigation_anchor = std::nullopt);
    void scroll_to_cursor(
        float wheel_delta, QVec2 local_mouse,
        std::optional<QVec3> navigation_anchor = std::nullopt);
    void focus_origin(float distance = 64.0f);
    void focus_bounds(QVec3 min_bounds, QVec3 max_bounds, float margin = 1.25f);
    [[nodiscard]] bool focus_optional_bounds(
        const std::optional<ViewportFocusBounds> &bounds,
        ViewportEmptyFocusBehavior empty_behavior =
            ViewportEmptyFocusBehavior::KeepCamera,
        float margin = 1.25f);
    void view_axis(ViewportViewAxis axis, int sign,
                   ViewportAxisLock lock = ViewportAxisLock::Locked);
    void unlock_orthographic_view();
    void set_view_direction(QVec3 forward_direction);
    void update_drag_delta(QVec2 delta);
    void update_drag_delta(QVec2 delta, ViewportDragMode mode);
    void fly_move(QVec3 local_direction, float delta_seconds);
    void adjust_fly_speed(float wheel_steps);
    bool set_default_fly_speed(float speed);
    void reset_fly_speed();
    /// Applies scene-authored clips while retaining any same-framing safety
    /// constraints owned by the camera controller.
    void set_clip_planes(float near_plane, float far_plane);
    bool restore_perspective_pose(const ViewportCameraPose &pose);

    const ViewportCameraState& state() const { return state_; }
    QVec2 viewport_size() const { return {viewport_width_, viewport_height_}; }
    float fly_speed() const { return fly_speed_; }
    float default_fly_speed() const { return default_fly_speed_; }
    bool fly_speed_is_default() const;
    ViewportCameraPose pose() const;
    ViewportRay ray_from_viewport_point(QVec2 local_mouse) const;
    ViewportScreenPoint project_world_point(QVec3 world_point) const;
    /// Clips to the camera depth range and viewport; returns no value if fully outside.
    std::optional<ViewportScreenSegment>
    project_visible_world_segment(QVec3 start, QVec3 end) const;
    bool is_dragging() const { return drag_mode_ != ViewportDragMode::None; }
    bool orthographic_view_locked() const { return orthographic_view_locked_; }

  private:
    QVec3 forward() const;
    QVec3 right() const;
    QVec3 up() const;
    void set_camera_basis(QVec3 forward_direction, QVec3 up_direction);
    void sync_basis_from_angles();
    void sync_angles_from_basis();
    void establish_zoom_envelope_from_current_view();
    void extend_zoom_envelope_to_current_view();
    void ensure_perspective_target_within_clip_range();
    void apply_clip_plane_constraints();
    std::optional<QVec3> rebase_navigation_depth(QVec3 navigation_anchor);
    std::optional<QVec3>
    repair_navigation_depth(QVec3 navigation_anchor);
    std::optional<QVec3> accept_pending_navigation_anchor();
    void clear_orbit_snap_source();

    ViewportCameraState state_;
    float configured_near_plane_ = 0.05f;
    float configured_far_plane_ = 1000.0f;
    float transient_far_plane_floor_ = 0.0f;
    float minimum_perspective_zoom_distance_ = 0.0f;
    float maximum_perspective_zoom_distance_ = 0.0f;
    float minimum_orthographic_zoom_size_ = 0.0f;
    float maximum_orthographic_zoom_size_ = 0.0f;
    QVec3 camera_forward_ = {0.0f, 0.0f, -1.0f};
    QVec3 camera_up_ = {0.0f, 1.0f, 0.0f};
    QVec3 pivot_ = {};
    std::optional<QVec3> pending_navigation_anchor_;
    bool pending_navigation_anchor_repair_only_ = false;
    std::optional<QVec3> orbit_anchor_;
    float viewport_width_ = 1.0f;
    float viewport_height_ = 1.0f;
    float fly_speed_ = kDefaultViewportFlyNavigationSpeed;
    float default_fly_speed_ = kDefaultViewportFlyNavigationSpeed;
    QVec2 last_mouse_ = {};
    ViewportDragMode drag_mode_ = ViewportDragMode::None;
    bool orthographic_view_locked_ = false;
    ViewportCameraPose orbit_snap_source_pose_ = {};
    bool has_orbit_snap_source_ = false;
};

} // namespace quader
