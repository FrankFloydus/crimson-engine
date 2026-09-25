////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "core/command_result.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class EditorHistoryService;

inline constexpr std::string_view kCreatePrimitiveToolId = "create_primitive";

inline constexpr std::string_view kPrimitiveTypeSetting = "primitive.type";
inline constexpr std::string_view kPrimitiveFlipFacesSetting =
    "primitive.flip_faces";
inline constexpr std::string_view kBoxSubdivisionsXSetting =
    "primitive.box.subdivisions_x";
inline constexpr std::string_view kBoxSubdivisionsYSetting =
    "primitive.box.subdivisions_y";
inline constexpr std::string_view kBoxSubdivisionsZSetting =
    "primitive.box.subdivisions_z";
inline constexpr std::string_view kCylinderSideCountSetting =
    "primitive.cylinder.side_count";
inline constexpr std::string_view kCylinderCapsSetting =
    "primitive.cylinder.caps";
inline constexpr std::string_view kConeSideCountSetting =
    "primitive.cone.side_count";
inline constexpr std::string_view kConeCapsSetting = "primitive.cone.caps";
inline constexpr std::string_view kSphereTypeSetting = "primitive.sphere.type";
inline constexpr std::string_view kSphereSubdivisionSetting =
    "primitive.sphere.subdivision";
inline constexpr std::string_view kSphereRadialSegmentsSetting =
    "primitive.sphere.radial_segments";
inline constexpr std::string_view kSphereRingsSetting =
    "primitive.sphere.rings";
inline constexpr std::string_view kSphereHemisphereSetting =
    "primitive.sphere.hemisphere";
inline constexpr std::string_view kTorusMajorSegmentsSetting =
    "primitive.torus.major_segments";
inline constexpr std::string_view kTorusMinorSegmentsSetting =
    "primitive.torus.minor_segments";
inline constexpr std::string_view kTorusTubeRadiusRatioSetting =
    "primitive.torus.tube_radius_ratio";

inline constexpr int kPrimitiveMinimumSubdivisionCount = 1;
inline constexpr int kPrimitiveMaximumSubdivisionCount = 128;
inline constexpr int kCylinderMinimumSideCount = 3;
inline constexpr int kCylinderMaximumSideCount = 128;
inline constexpr int kConeMinimumSideCount = 3;
inline constexpr int kConeMaximumSideCount = 128;
inline constexpr int kSphereMinimumRadialSegments = 3;
inline constexpr int kSphereMaximumRadialSegments = 128;
inline constexpr int kSphereMinimumRings = 2;
inline constexpr int kSphereMaximumRings = 128;
inline constexpr int kSphereMinimumSubdivision = 1;
inline constexpr int kSphereMaximumSubdivision = 6;
inline constexpr int kTorusMinimumMajorSegments = 3;
inline constexpr int kTorusMaximumMajorSegments = 128;
inline constexpr int kTorusMinimumMinorSegments = 3;
inline constexpr int kTorusMaximumMinorSegments = 128;
inline constexpr double kTorusMinimumTubeRadiusRatio = 0.01;
inline constexpr double kTorusMaximumTubeRadiusRatio = 0.49;

/// Identifies the authored primitive generated from one footprint.
enum class PrimitiveCreationKind {
  Box = 0,
  Cylinder = 1,
  Sphere = 2,
  Cone = 3,
  Torus = 4,
};

/// Selects the topology family used by authored spheres.
enum class PrimitiveSphereType { Uv = 0, Quad = 1 };

/// Shared release gates for primitive activation and retained settings choices.
[[nodiscard]] bool
primitive_creation_available_in_release(PrimitiveCreationKind kind) noexcept;
[[nodiscard]] bool
primitive_sphere_type_available_in_release(PrimitiveSphereType type) noexcept;
[[nodiscard]] bool primitive_setting_available_in_release(
    std::string_view name, double value) noexcept;

/// Returns the canonical scene-name stem for a generated primitive kind.
[[nodiscard]] std::string_view
primitive_creation_default_name(PrimitiveCreationKind kind);

/// Stores session settings used by new and still-live Box primitives.
struct BoxPrimitiveCreationSettings {
  int subdivisions_x = 1;
  int subdivisions_y = 1;
  int subdivisions_z = 1;
  bool flip_faces = false;
};

/// Identifies a local face-normal axis of the editable cube.
enum class PrimitiveCubeAxis { X, Y, Z };

inline constexpr double kPrimitiveCubeMinimumExtent = 0.0001;
inline constexpr double kPrimitiveMinimumExtent = kPrimitiveCubeMinimumExtent;

/// Identifies the bound or radial extent owned by one immutable resize baseline.
enum class PrimitiveResizeControl {
  PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ, Radius,
  OuterRadius, InnerRadius
};

/// Bounds describe the authored volume, including radial shapes' full ellipses.
struct PrimitiveCreationState {
  int modeling_object_id = -1;
  PrimitiveCreationKind kind = PrimitiveCreationKind::Box;
  QVec3 local_min;
  QVec3 local_max;
  std::array<int, 3> subdivisions = {1, 1, 1};
  int side_count = 32;
  bool caps = true;
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  int major_segments = 32;
  int minor_segments = 12;
  double tube_radius_ratio = 0.2;
  bool flip_faces = false;
  bool resizing = false;
  std::uint64_t revision = 0;
  PrimitiveSphereType sphere_type = PrimitiveSphereType::Uv;
  int sphere_subdivision = 3;
};

/// Projects the live cube's owner state without compiling or copying its mesh.
struct PrimitiveCubeState {
  int modeling_object_id = -1;
  QVec3 local_min;
  QVec3 local_max;
  std::array<int, 3> subdivisions = {1, 1, 1};
  bool flip_faces = false;
  bool resizing = false;
  std::uint64_t revision = 0;
};

/// Stores session settings used by new and still-live Cylinders.
struct CylinderPrimitiveCreationSettings {
  int side_count = 32;
  bool caps = true;
  bool flip_faces = false;
};

/// Stores session settings used by new and still-live Cones.
struct ConePrimitiveCreationSettings {
  int side_count = 32;
  bool caps = true;
  bool flip_faces = false;
};

/// Stores session settings used by new and still-live Spheres.
struct SpherePrimitiveCreationSettings {
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  bool flip_faces = false;
  PrimitiveSphereType type = PrimitiveSphereType::Uv;
  int subdivision = 3;
};

/// Stores session settings used by new and still-live Tori.
struct TorusPrimitiveCreationSettings {
  int major_segments = 32;
  int minor_segments = 12;
  double tube_radius_ratio = 0.2;
  bool flip_faces = false;
};

/// Remembers only authored tool options for the lifetime of the application.
/// Placement, live objects and unfinished gestures remain session-owned.
struct PrimitiveCreationSessionDefaults {
  PrimitiveCreationKind preferred_kind = PrimitiveCreationKind::Box;
  BoxPrimitiveCreationSettings box;
  CylinderPrimitiveCreationSettings cylinder;
  ConePrimitiveCreationSettings cone;
  SpherePrimitiveCreationSettings sphere;
  TorusPrimitiveCreationSettings torus;
};

/**
 * Owns one modal primitive session with bounded per-shape undo history.
 *
 * Accepted shapes can be undone and redone without leaving creation mode.
 * Apply publishes those steps to editor history; Cancel and per-shape failures
 * restore the pre-session state. The newest accepted primitive remains
 * live-editable only while its sole active selection revision is unchanged.
 * Accepted settings persist between sessions, including canceled sessions.
 */
class PrimitiveCreationToolService final {
public:
  PrimitiveCreationToolService(
      EditorHistoryService &history, ModelingSessionService &modeling,
      ModelingOperationPreviewCheckpointHooks checkpoint_hooks = {},
      PrimitiveCreationSessionDefaults *session_defaults = nullptr);

  [[nodiscard]] bool active() const { return session_.has_value(); }
  [[nodiscard]] bool can_apply() const;
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  [[nodiscard]] bool can_reset_settings() const;
  /// Retires local undo payloads using the editor's current retention limit.
  /// The complete session baseline remains available to Cancel.
  void apply_history_limit();
  [[nodiscard]] std::size_t shape_count() const;
  [[nodiscard]] bool owns_created_object(int modeling_object_id) const;
  [[nodiscard]] std::optional<PrimitiveCreationKind> active_kind() const;
  /// Active shape, or the latest application-owned choice when idle.
  [[nodiscard]] PrimitiveCreationKind preferred_kind() const;
  [[nodiscard]] std::string_view active_tool_id() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const {
    return tool_settings_revision_;
  }

  [[nodiscard]] const BoxPrimitiveCreationSettings &box_settings() const {
    return box_settings_;
  }
  [[nodiscard]] const CylinderPrimitiveCreationSettings &
  cylinder_settings() const {
    return cylinder_settings_;
  }
  [[nodiscard]] const ConePrimitiveCreationSettings &cone_settings() const {
    return cone_settings_;
  }
  [[nodiscard]] const SpherePrimitiveCreationSettings &sphere_settings() const {
    return sphere_settings_;
  }
  [[nodiscard]] const TorusPrimitiveCreationSettings &torus_settings() const {
    return torus_settings_;
  }

  CommandResult activate();
  /// Finishes accepted shapes and discards unfinished gestures before switching.
  CommandResult activate(PrimitiveCreationKind kind);
  CommandResult set_kind(PrimitiveCreationKind kind);
  /// Accepts an explicit volume, preserving all supplied dimensions.
  CommandResult accept_preview(const ModelingBoxPreview &preview,
                               std::string name,
                               MaterialReference material = {});
  /// Completes a drawn footprint with its base fixed. Sphere height uses the
  /// smaller diameter (half for hemisphere); Torus uses diameter * tube/outer
  /// radius ratio. Both X/Z spans remain intact, and later edits are unrestricted.
  CommandResult accept_footprint(const ModelingBoxPreview &preview,
                                std::string name,
                                MaterialReference material = {});
  CommandResult apply();
  CommandResult cancel();
  CommandResult undo();
  CommandResult redo();
  CommandResult set_setting(std::string_view name, double value);
  CommandResult reset_settings();

  [[nodiscard]] std::optional<PrimitiveCreationState>
  live_primitive_creation() const;
  CommandResult begin_resize(PrimitiveResizeControl control,
                             QVec3 radial_axis = {1.0f, 0.0f, 0.0f});
  /// Local-axis displacement, or radial distance change along the starting ray.
  CommandResult update_resize(double displacement);
  CommandResult end_resize();
  CommandResult cancel_resize();
  /// Toggles Cylinder/Cone caps; during drawing, affects only the pending shape.
  CommandResult toggle_caps();
  /// Toggles the Sphere generator's hemisphere setting, preserving its volume.
  CommandResult toggle_hemisphere();

  /// Box-only compatibility interface for existing viewport clients.
  [[nodiscard]] std::optional<PrimitiveCubeState> live_cube() const;
  CommandResult begin_cube_resize(PrimitiveCubeAxis axis, bool positive);
  /// Signed local-axis displacement from the beginning of this gesture.
  CommandResult update_cube_resize(double displacement);
  CommandResult end_cube_resize();
  CommandResult cancel_cube_resize();
  /// During a footprint, changes only the pending shape.
  CommandResult flip_faces();

private:
  /// Retains the final geometry of one shape, including its live settings edits.
  struct ShapeHistory {
    ModelingSessionMemento before;
    ModelingSessionMemento after;
  };

  /// Keeps complete rollback/ownership separately from bounded local history.
  struct Session {
    PrimitiveCreationKind kind = PrimitiveCreationKind::Box;
    ModelingSessionMemento before;
    std::size_t accepted_shapes = 0;
    std::vector<int> created_object_ids;
    std::vector<ShapeHistory> shapes;
    std::size_t history_cursor = 0;
  };

  /// Keeps the newest accepted primitive editable while its selection is
  /// unchanged.
  struct LivePrimitive {
    ModelingBoxPreview placement;
    int modeling_object_id = -1;
    std::uint64_t selection_revision = 0;
    QVec3 local_min;
    QVec3 local_max;

    /// Restores dimensions and torus ratio; other setting edits survive.
    struct ResizeBaseline {
      PrimitiveResizeControl control = PrimitiveResizeControl::PositiveX;
      QVec3 local_min;
      QVec3 local_max;
      QVec3 radial_axis = {1.0f, 0.0f, 0.0f};
      double radial_distance = 0.0;
      double tube_radius_ratio = 0.2;
    };
    std::optional<ResizeBaseline> resize;
  };

  CommandResult accept_volume(const ModelingBoxPreview &preview, std::string name,
                              MaterialReference material,
                              bool use_footprint_proportions);
  [[nodiscard]] CommandResult rollback_and_exit(std::string message,
                                                bool report_failure);
  [[nodiscard]] bool live_primitive_is_selected() const;
  [[nodiscard]] CommandResult
  update_live_primitive(bool preserve_selection = false);
  [[nodiscard]] CommandResult freeze_live_primitive_if_selection_changed();
  [[nodiscard]] CommandResult reset_footprint();
  [[nodiscard]] CommandResult restore_shape_history(
      const ModelingSessionMemento &memento);
  void finish_session();
  void finish_checkpoint(bool restored_by_cancel);
  void load_settings(const PrimitiveCreationSessionDefaults &settings);
  void remember_settings();

  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  ModelingOperationPreviewCheckpointHooks checkpoint_hooks_;
  PrimitiveCreationSessionDefaults accepted_settings_;
  PrimitiveCreationSessionDefaults *session_defaults_ = nullptr;
  BoxPrimitiveCreationSettings box_settings_;
  CylinderPrimitiveCreationSettings cylinder_settings_;
  ConePrimitiveCreationSettings cone_settings_;
  SpherePrimitiveCreationSettings sphere_settings_;
  TorusPrimitiveCreationSettings torus_settings_;
  PrimitiveCreationKind preferred_kind_ = PrimitiveCreationKind::Box;
  std::optional<Session> session_;
  std::optional<LivePrimitive> live_primitive_;
  std::uint64_t tool_settings_revision_ = 0;
  std::uint64_t primitive_revision_ = 0;
};

} // namespace quader
